#include "ClusterHarness.h"

#include <algorithm>

namespace raft {
namespace test {

// ============================================================
// Construction: create N nodes with MemoryStorage
// ============================================================

ClusterHarness::ClusterHarness(std::vector<NodeId> peers,
                                uint32_t electionTick,
                                uint32_t heartbeatTick)
    : peerIds_(std::move(peers))
    , electionTick_(electionTick)
    , heartbeatTick_(heartbeatTick) {

    for (auto id : peerIds_) {
        auto storage = std::make_unique<MemoryStorage>();
        ConfState cs;
        cs.nodes = peerIds_;
        storage->setInitialState(HardState{}, cs);

        Config rc;
        rc.id             = id;
        rc.electionTick   = electionTick;
        rc.heartbeatTick  = heartbeatTick;
        rc.storage        = storage.get();
        rc.maxSizePerMsg  = 1ull << 20;
        rc.maxInflightMsgs = 256;
        rc.peers          = peerIds_;

        storages_[id] = std::move(storage);
        nodes_[id]    = std::make_unique<RawNode>(rc);
    }
}

// ============================================================
// Step: tick all nodes, deliver messages
// ============================================================

void ClusterHarness::step(uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) {
        // 1. Tick each node.
        for (auto& [id, node] : nodes_) {
            node->tick();
        }
        // 2. Drain each node's Ready, route messages to the queue.
        for (auto& [id, node] : nodes_) {
            if (!node->hasReady()) continue;
            Ready rd = node->poll();
            if (!rd.entries.empty()) {
                storages_[id]->append(rd.entries);
            }
            for (const auto& m : rd.messages) {
                msgQueue_.push(m);
            }
            node->advance();
        }
        // 3. Deliver queued messages to their targets.
        deliver();
    }
    // Final drain: persist any entries produced by the last deliver().
    for (auto& [id, node] : nodes_) {
        if (!node->hasReady()) continue;
        Ready rd = node->poll();
        if (!rd.entries.empty()) {
            storages_[id]->append(rd.entries);
        }
        node->advance();
    }
}

void ClusterHarness::deliver() {
    // Drain the queue. New messages produced during step() go to a fresh
    // queue, delivered on the next step() call.
    std::queue<Message> q;
    q.swap(msgQueue_);

    while (!q.empty()) {
        Message m = q.front();
        q.pop();

        // Local messages bypass the drop filter (they're internal signals).
        bool isLocal = (m.type == MessageType::MsgUnreachable ||
                        m.type == MessageType::MsgSnapStatus  ||
                        m.type == MessageType::MsgCheckQuorum);
        if (!isLocal && dropFilter_ && dropFilter_(m.from, m.to)) {
            Message unreachable;
            unreachable.type = MessageType::MsgUnreachable;
            unreachable.from  = m.to;
            unreachable.to    = m.from;
            msgQueue_.push(unreachable);
            continue;
        }

        auto it = nodes_.find(m.to);
        if (it != nodes_.end()) {
            it->second->step(m);
        }
    }
}

// ============================================================
// Propose + accessors
// ============================================================

bool ClusterHarness::propose(NodeId id, const std::vector<uint8_t>& data) {
    auto it = nodes_.find(id);
    if (it == nodes_.end()) return false;
    return it->second->propose(data);
}

RawNode& ClusterHarness::node(NodeId id) {
    auto it = nodes_.find(id);
    if (it == nodes_.end()) throw std::out_of_range("no such node");
    return *it->second;
}

MemoryStorage& ClusterHarness::storage(NodeId id) {
    auto it = storages_.find(id);
    if (it == storages_.end()) throw std::out_of_range("no such storage");
    return *it->second;
}

NodeId ClusterHarness::leader() const {
    for (const auto& [id, node] : nodes_) {
        if (node->role() == Role::Leader) return id;
    }
    return 0;
}

} // namespace test
} // namespace raft
