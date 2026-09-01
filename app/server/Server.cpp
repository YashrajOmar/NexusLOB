#include "server/Server.h"
#include "protocol/Protocol.h"

#include <chrono>
#include <iostream>
#include <stdexcept>

namespace app {

namespace {
constexpr auto tickInterval = std::chrono::milliseconds(100);
} // namespace

// ============================================================
// Construction: wire all components together
// ============================================================

Server::Server(const AppConfig& config)
    : config_(config)
    , wal_(config.dataDir)
    , snapFile_(config.dataDir)
    , fsm_()
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
    // Restore from snapshot if one exists.
    auto snap = snapFile_.load();
    if (snap.has_value()) {
        fsm_.restore(snap->data);
        wal_.applySnapshot(*snap);
    }

    // Wire transport callback → queue incoming messages.
    transport_.setRecvCallback([this](const raft::Message& m) {
        onRaftMessage(m);
    });
    transport_.setPeers(config_.peerAddrMap());
    transport_.start();

    // Wire client proposal handler.
    clientServer_.setProposalHandler(
        [this](const ClientRequest& req) { return onClientRequest(req); });
    clientServer_.start();

    running_ = true;
    loopThread_ = std::thread([this] { raftLoop(); });
}

void Server::stop() {
    running_ = false;
    msgCond_.notify_all();
    pendingCv_.notify_all();
    if (loopThread_.joinable()) {
        loopThread_.join();
    }
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
// Raft loop: tick, drain messages, poll Ready, process, advance
// ============================================================

void Server::raftLoop() {
    while (running_) {
        // 1. Advance the logical clock.
        {
            std::lock_guard<std::mutex> lock(raftMutex_);
            node_.tick();
        }

        // 2. Drain any received messages into the RawNode.
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
            for (const auto& m : drained) {
                node_.step(m);
            }
        }

        // 3. Poll for Ready and process it.
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

// ============================================================
// Transport callback → queue message for the raft loop
// ============================================================

void Server::onRaftMessage(const raft::Message& m) {
    {
        std::lock_guard<std::mutex> lock(msgMutex_);
        pendingMsgs_.push_back(m);
    }
    msgCond_.notify_one();
}

// ============================================================
// Client request handler → propose + wait for apply
// ============================================================

ClientResponse Server::onClientRequest(const ClientRequest& req) {
    ClientResponse resp;

    if (!isLeader()) {
        resp.ok = false;
        resp.err = "not leader";
        resp.leaderHint = leaderId();
        return resp;
    }

    // Encode the command into KV wire bytes.
    std::string data;
    switch (req.op) {
        case ClientRequest::SET:
            data = protocol::encodeSet(req.key, req.value);
            break;
        case ClientRequest::GET:
            data = protocol::encodeGet(req.key);
            break;
        case ClientRequest::DEL:
            data = protocol::encodeDel(req.key);
            break;
    }

    // Propose into raft and record the index we're waiting for.
    raft::Index idx;
    auto pending = std::make_shared<PendingProposal>();
    {
        std::lock_guard<std::mutex> lock(raftMutex_);
        bool ok = node_.propose(std::vector<uint8_t>(data.begin(), data.end()));
        if (!ok) {
            resp.ok = false;
            resp.err = "not leader";
            resp.leaderHint = leaderId();
            return resp;
        }
        idx = node_.lastIndex();
        {
            std::lock_guard<std::mutex> plock(pendingMutex_);
            pending_[idx] = pending;
        }
    }

    // Wait for the entry to commit and apply.
    {
        std::unique_lock<std::mutex> lock(pendingMutex_);
        pendingCv_.wait(lock, [&] { return pending->done || !running_; });
    }

    if (!pending->done) {
        resp.ok = false;
        resp.err = "shutting down";
        return resp;
    }

    return pending->resp;
}

// ============================================================
// Ready processing: persist, send, apply
// ============================================================

void Server::processReady(const raft::Ready& rd) {
    // 1. Persist entries and hardstate before sending messages (safety).
    if (!rd.entries.empty()) {
        wal_.append(rd.entries);
    }

    // Always save hardstate (cheap, and term changes matter).
    if (rd.hardState.term != 0 || rd.hardState.commit != 0) {
        wal_.saveHardState(rd.hardState);
    }

    // Apply snapshot if present.
    if (rd.snapshot.has_value()) {
        fsm_.restore(rd.snapshot->data);
        snapFile_.save(*rd.snapshot);
        wal_.applySnapshot(*rd.snapshot);
    }

    // 2. Send messages (after persistence).
    for (const auto& m : rd.messages) {
        transport_.send(m);
    }

    // 3. Apply committed entries.
    applyCommittedEntries(rd.committedEntries);
}

// ============================================================
// Apply committed entries → wake waiting clients
// ============================================================

void Server::applyCommittedEntries(const std::vector<raft::Entry>& entries) {
    for (const auto& e : entries) {
        auto result = fsm_.apply(e.data);

        // Wake any client waiting on this index.
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
            bool found = false;
            std::string value;
            protocol::decodeApplyResult(
                std::string(result.begin(), result.end()), found, value);
            pending->resp.ok     = true;
            pending->resp.found  = found;
            pending->resp.value  = value;
            pending->resp.leaderHint = 0;
            pending->done        = true;
            pendingCv_.notify_one();
        }
    }
}

} // namespace app
