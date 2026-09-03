#include "server/ShardManager.h"
#include "statemachine/MapOrderBook.h"

#include <chrono>
#include <iostream>
#include <stdexcept>

namespace app {

namespace {
constexpr auto tickInterval = std::chrono::milliseconds(100);
}

ShardManager::ShardManager(const AppConfig& config, uint32_t electionTick,
                           uint32_t heartbeatTick)
    : config_(config)
    , electionTick_(electionTick)
    , heartbeatTick_(heartbeatTick) {}

ShardManager::~ShardManager() {
    stop();
}

void ShardManager::createShard(const std::string& symbol) {
    auto shard = std::make_unique<Shard>();

    // Each shard gets its own subdirectory for WAL files.
    std::string shardDir = config_.dataDir + "/" + symbol;
    shard->wal = std::make_unique<WriteAheadLog>(shardDir);
    shard->snapFile = std::make_unique<SnapshotFile>(shardDir);

    // Each shard gets its own LOBStateMachine with the symbol.
    shard->fsm = std::make_unique<LOBStateMachine>(
        std::make_unique<MapOrderBook>(symbol));

    // Each shard gets its own RawNode.
    raft::Config rc;
    auto bc = config_.toRaftConfig();
    rc.id             = bc.id;
    rc.electionTick   = electionTick_;
    rc.heartbeatTick  = heartbeatTick_;
    rc.storage        = shard->wal.get();
    rc.maxSizePerMsg  = bc.maxSizePerMsg;
    rc.maxInflightMsgs = bc.maxInflightMsgs;
    rc.peers          = bc.peerIds;

    shard->node = std::make_unique<raft::RawNode>(rc);

    shards_[symbol] = std::move(shard);
}

void ShardManager::start() {
    std::lock_guard<std::mutex> lock(shardsMutex_);
    for (auto& [sym, shard] : shards_) {
        shard->running = true;
        shard->loopThread = std::thread([this, shard = shard.get()] {
            shardLoop(shard);
        });
    }
}

void ShardManager::stop() {
    {
        std::lock_guard<std::mutex> lock(shardsMutex_);
        for (auto& [sym, shard] : shards_) {
            shard->running = false;
            shard->pendingCv.notify_all();
        }
    }
    {
        std::lock_guard<std::mutex> lock(shardsMutex_);
        for (auto& [sym, shard] : shards_) {
            if (shard->loopThread.joinable())
                shard->loopThread.join();
        }
    }
}

void ShardManager::shardLoop(Shard* shard) {
    while (shard->running) {
        shard->node->tick();

        if (shard->node->hasReady()) {
            auto rd = shard->node->poll();
            processReady(shard, rd);
            shard->node->advance();
        }

        std::this_thread::sleep_for(tickInterval);
    }
}

void ShardManager::processReady(Shard* shard, const raft::Ready& rd) {
    if (!rd.entries.empty()) shard->wal->appendNoSync(rd.entries);
    if (rd.hardState.term != 0 || rd.hardState.commit != 0)
        shard->wal->saveHardState(rd.hardState);
    if (!rd.entries.empty() || rd.hardState.term != 0 || rd.hardState.commit != 0)
        shard->wal->sync();

    if (rd.snapshot.has_value()) {
        shard->fsm->restore(rd.snapshot->data);
        shard->snapFile->save(*rd.snapshot);
        shard->wal->applySnapshot(*rd.snapshot);
    }

    for (const auto& e : rd.committedEntries) {
        auto result = shard->fsm->apply(e.data);

        std::shared_ptr<Pending> pending;
        {
            std::lock_guard<std::mutex> lock(shard->pendingMutex);
            auto it = shard->pending.find(e.index);
            if (it != shard->pending.end()) {
                pending = it->second;
                shard->pending.erase(it);
            }
        }
        if (pending) {
            pending->result = std::move(result);
            pending->done = true;
            shard->pendingCv.notify_all();
        }
    }
}

std::vector<uint8_t> ShardManager::submit(const std::string& symbol,
                                          const std::vector<uint8_t>& cmd) {
    Shard* shard = nullptr;
    {
        std::lock_guard<std::mutex> lock(shardsMutex_);
        auto it = shards_.find(symbol);
        if (it == shards_.end()) {
            createShard(symbol);
            it = shards_.find(symbol);
        }
        shard = it->second.get();
    }

    if (shard->node->role() != raft::Role::Leader) return {};

    raft::Index idx;
    auto pending = std::make_shared<Pending>();
    {
        if (!shard->node->propose(cmd)) return {};
        idx = shard->node->lastIndex();
        std::lock_guard<std::mutex> lock(shard->pendingMutex);
        shard->pending[idx] = pending;
    }

    {
        std::unique_lock<std::mutex> lock(shard->pendingMutex);
        shard->pendingCv.wait(lock, [&] { return pending->done || !shard->running; });
    }

    if (!pending->done) return {};
    return pending->result;
}

LOBStateMachine* ShardManager::getFSM(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(shardsMutex_);
    auto it = shards_.find(symbol);
    if (it == shards_.end()) return nullptr;
    return it->second->fsm.get();
}

std::vector<std::string> ShardManager::symbols() const {
    std::lock_guard<std::mutex> lock(shardsMutex_);
    std::vector<std::string> out;
    for (const auto& [sym, shard] : shards_) out.push_back(sym);
    return out;
}

size_t ShardManager::shardCount() const {
    std::lock_guard<std::mutex> lock(shardsMutex_);
    return shards_.size();
}

bool ShardManager::isLeader() const {
    std::lock_guard<std::mutex> lock(shardsMutex_);
    for (const auto& [sym, shard] : shards_) {
        if (shard->node->role() == raft::Role::Leader) return true;
    }
    return false;
}

} // namespace app
