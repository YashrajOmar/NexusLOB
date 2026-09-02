#include "server/Server.h"

#include <chrono>
#include <iostream>
#include <stdexcept>

namespace app {

namespace {
constexpr auto tickInterval = std::chrono::milliseconds(100);
} // namespace

// ============================================================
// Construction: inject all mode-specific components
// ============================================================

Server::Server(const AppConfig& config,
               std::unique_ptr<StateMachine> fsm,
               std::unique_ptr<CommandCodec> codec,
               BookReader* bookReader,
               std::unique_ptr<OrderClientServer> orderServer,
               ReadMode readMode)
    : config_(config)
    , readMode_(readMode)
    , wal_(config.dataDir)
    , snapFile_(config.dataDir)
    , fsm_(std::move(fsm))
    , codec_(std::move(codec))
    , bookReader_(bookReader)
    , orderServer_(std::move(orderServer))
    , transport_(config.selfId, config.self()->raftPort)
    , clientServer_(config.self()->clientPort)
    , node_([&] {
        raft::Config rc;
        auto bc = config.toRaftConfig();
        rc.id             = bc.id;
        rc.electionTick   = bc.electionTick;
        rc.heartbeatTick  = bc.heartbeatTick;
        rc.storage        = &wal_;
        rc.maxSizePerMsg  = bc.maxSizePerMsg;
        rc.maxInflightMsgs = bc.maxInflightMsgs;
        rc.peers          = bc.peerIds;
        rc.readOnlyOption = (readMode == ReadMode::ReadIndex)
                            ? raft::ReadOnlyOption::Safe
                            : raft::ReadOnlyOption::LeaseBased;
        return rc;
    }()) {
}

Server::~Server() {
    stop();
}

// ============================================================
// Start / stop
// ============================================================

void Server::start() {
    auto snap = snapFile_.load();
    if (snap.has_value()) {
        fsm_->restore(snap->data);
        wal_.applySnapshot(*snap);
    }

    transport_.setRecvCallback([this](const raft::Message& m) {
        onRaftMessage(m);
    });
    transport_.setPeers(config_.peerAddrMap());
    transport_.start();

    clientServer_.setProposalHandler(
        [this](const ClientRequest& req) { return onClientRequest(req); });
    clientServer_.start();

    // Wire the binary order server (if injected).
    if (orderServer_) {
        orderServer_->setSubmitCallback(
            [this](const std::vector<uint8_t>& cmd) { return submitRaw(cmd); });
        orderServer_->setReadBookCallback(
            [this](size_t maxLevels) {
                auto b = readBids(maxLevels);
                auto a = readAsks(maxLevels);
                return std::make_pair(b, a);
            });
        orderServer_->setIsLeaderCallback(
            [this]() { return isLeader(); });
        orderServer_->start();
    }

    running_ = true;
    loopThread_ = std::thread([this] { raftLoop(); });
}

void Server::stop() {
    running_ = false;
    msgCond_.notify_all();
    pendingCv_.notify_all();
    readCv_.notify_all();
    if (loopThread_.joinable()) loopThread_.join();
    if (orderServer_) orderServer_->stop();
    transport_.stop();
    clientServer_.stop();
}

bool Server::isLeader() const {
    return node_.role() == raft::Role::Leader;
}

raft::NodeId Server::leaderId() const {
    return node_.lead();
}

// ============================================================
// Raft loop
// ============================================================

void Server::raftLoop() {
    while (running_) {
        {
            std::lock_guard<std::mutex> lock(raftMutex_);
            node_.tick();
        }

        std::vector<raft::Message> drained;
        {
            std::unique_lock<std::mutex> lock(msgMutex_);
            msgCond_.wait_for(lock, tickInterval, [this] {
                return !pendingMsgs_.empty() || !running_;
            });
            drained.swap(pendingMsgs_);
        }

        if (!drained.empty()) {
            std::lock_guard<std::mutex> lock(raftMutex_);
            for (const auto& m : drained) node_.step(m);
        }

        if (node_.hasReady()) {
            raft::Ready rd;
            {
                std::lock_guard<std::mutex> lock(raftMutex_);
                rd = node_.poll();
            }
            processReady(rd);
            {
                std::lock_guard<std::mutex> lock(raftMutex_);
                node_.advance();
            }
        }
    }
}

void Server::onRaftMessage(const raft::Message& m) {
    {
        std::lock_guard<std::mutex> lock(msgMutex_);
        pendingMsgs_.push_back(m);
    }
    msgCond_.notify_one();
}

// ============================================================
// Client request handler (text protocol — uses codec)
// ============================================================

ClientResponse Server::onClientRequest(const ClientRequest& req) {
    ClientResponse resp;
    if (!isLeader()) {
        resp.ok = false;
        resp.err = "not leader";
        resp.leaderHint = leaderId();
        return resp;
    }
    auto data = codec_->encode(req);
    if (data.empty()) {
        resp.ok = false;
        resp.err = "bad request";
        return resp;
    }
    raft::Index idx;
    auto pending = std::make_shared<PendingProposal>();
    {
        std::lock_guard<std::mutex> lock(raftMutex_);
        if (!node_.propose(data)) {
            resp.ok = false;
            resp.err = "not leader";
            resp.leaderHint = leaderId();
            return resp;
        }
        idx = node_.lastIndex();
        std::lock_guard<std::mutex> plock(pendingMutex_);
        pending_[idx] = pending;
    }
    {
        std::unique_lock<std::mutex> lock(pendingMutex_);
        pendingCv_.wait(lock, [&] { return pending->done || !running_; });
    }
    if (!pending->done) {
        resp.ok = false;
        resp.err = "shutting down";
        return resp;
    }
    return codec_->decodeResult(pending->rawResult);
}

// ============================================================
// Submit raw bytes through raft (binary protocol path)
// ============================================================

std::vector<uint8_t> Server::submitRaw(const std::vector<uint8_t>& cmd) {
    if (!isLeader()) return {};
    raft::Index idx;
    auto pending = std::make_shared<PendingProposal>();
    {
        std::lock_guard<std::mutex> lock(raftMutex_);
        if (!node_.propose(cmd)) return {};
        idx = node_.lastIndex();
        std::lock_guard<std::mutex> plock(pendingMutex_);
        pending_[idx] = pending;
    }
    {
        std::unique_lock<std::mutex> lock(pendingMutex_);
        pendingCv_.wait(lock, [&] { return pending->done || !running_; });
    }
    if (!pending->done) return {};
    return pending->rawResult;
}

// ============================================================
// Read the order book — all 3 modes (Decision 4)
//
// Direct:     read local FSM immediately.
// LeaseBased: leader reads from lease (no quorum check).
// ReadIndex:  leader confirms quorum, waits for apply, then reads.
// ============================================================

std::vector<PriceLevel> Server::readBids(size_t maxLevels) {
    if (!bookReader_) return {};

    if (readMode_ == ReadMode::Direct || readMode_ == ReadMode::LeaseBased) {
        // Direct + LeaseBased: just read. LeaseBased trusts the leader's
        // lease (no quorum round-trip). Both read local FSM.
        return bookReader_->bids(maxLevels);
    }

    // ReadIndex (Safe): leader confirms quorum before reading.
    if (!isLeader()) return {};

    // Generate a unique context token for this read.
    std::vector<uint8_t> token(8);
    uint64_t t = readTokenCounter_.fetch_add(1);
    for (int i = 0; i < 8; ++i) token[i] = static_cast<uint8_t>((t >> (i*8)) & 0xFF);

    auto pending = std::make_shared<PendingRead>();
    pending->maxLevels = maxLevels;
    pending->isBids = true;
    {
        std::lock_guard<std::mutex> lock(readMutex_);
        pendingReads_[token] = pending;
    }
    {
        std::lock_guard<std::mutex> lock(raftMutex_);
        node_.readIndex(token);
    }

    // Wait for the ReadState to arrive in a future Ready.
    {
        std::unique_lock<std::mutex> lock(readMutex_);
        readCv_.wait(lock, [&] { return pending->done || !running_; });
    }
    if (!pending->done) return {};
    return pending->result;
}

std::vector<PriceLevel> Server::readAsks(size_t maxLevels) {
    if (!bookReader_) return {};

    if (readMode_ == ReadMode::Direct || readMode_ == ReadMode::LeaseBased) {
        return bookReader_->asks(maxLevels);
    }

    // ReadIndex (Safe).
    if (!isLeader()) return {};

    std::vector<uint8_t> token(8);
    uint64_t t = readTokenCounter_.fetch_add(1);
    for (int i = 0; i < 8; ++i) token[i] = static_cast<uint8_t>((t >> (i*8)) & 0xFF);

    auto pending = std::make_shared<PendingRead>();
    pending->maxLevels = maxLevels;
    pending->isBids = false;
    {
        std::lock_guard<std::mutex> lock(readMutex_);
        pendingReads_[token] = pending;
    }
    {
        std::lock_guard<std::mutex> lock(raftMutex_);
        node_.readIndex(token);
    }

    {
        std::unique_lock<std::mutex> lock(readMutex_);
        readCv_.wait(lock, [&] { return pending->done || !running_; });
    }
    if (!pending->done) return {};
    return pending->result;
}

// ============================================================
// Handle ReadStates from Ready (ReadIndex completion)
// ============================================================

void Server::handleReadStates(const std::vector<raft::ReadState>& readStates) {
    for (const auto& rs : readStates) {
        std::shared_ptr<PendingRead> pending;
        {
            std::lock_guard<std::mutex> lock(readMutex_);
            auto it = pendingReads_.find(rs.requestCtx);
            if (it != pendingReads_.end()) pending = it->second;
        }
        if (!pending) continue;

        // Wait until the FSM has applied up to rs.index.
        // In practice this may already be true. If not, the next
        // applyCommittedEntries will advance appliedIndex_.
        // We spin briefly (the index is usually already applied).
        while (appliedIndex_.load() < rs.index && running_) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        if (!running_) break;

        // Safe to read now — the FSM has caught up.
        if (pending->isBids)
            pending->result = bookReader_->bids(pending->maxLevels);
        else
            pending->result = bookReader_->asks(pending->maxLevels);

        pending->done = true;
        {
            std::lock_guard<std::mutex> lock(readMutex_);
            pendingReads_.erase(rs.requestCtx);
        }
        readCv_.notify_all();
    }
}

// ============================================================
// Ready processing: persist, send, apply, handle reads
// ============================================================

void Server::processReady(const raft::Ready& rd) {
    if (!rd.entries.empty()) wal_.append(rd.entries);
    if (rd.hardState.term != 0 || rd.hardState.commit != 0)
        wal_.saveHardState(rd.hardState);
    if (rd.snapshot.has_value()) {
        fsm_->restore(rd.snapshot->data);
        snapFile_.save(*rd.snapshot);
        wal_.applySnapshot(*rd.snapshot);
    }
    for (const auto& m : rd.messages) transport_.send(m);

    applyCommittedEntries(rd.committedEntries);
    handleReadStates(rd.readStates);
}

void Server::applyCommittedEntries(const std::vector<raft::Entry>& entries) {
    for (const auto& e : entries) {
        auto result = fsm_->apply(e.data);
        appliedIndex_.store(e.index);

        std::shared_ptr<PendingProposal> pending;
        {
            std::lock_guard<std::mutex> lock(pendingMutex_);
            auto it = pending_.find(e.index);
            if (it != pending_.end()) {
                pending = it->second;
                pending_.erase(it);
            }
        }
        if (pending) {
            pending->rawResult = std::move(result);
            pending->done = true;
            pendingCv_.notify_one();
        }
    }
}

} // namespace app
