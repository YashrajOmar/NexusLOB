#pragma once

#include "raft/RawNode.h"
#include "raft/Storage.h"

#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <random>
#include <queue>
#include <stdexcept>

namespace raft {
namespace test {

// ============================================================
// MemoryStorage (shared with InteractionTest.cpp)
// ============================================================

class MemoryStorage : public Storage {
public:
    MemoryStorage() {
        Entry dummy;
        dummy.index = 0;
        dummy.term  = 0;
        entries_.push_back(dummy);
    }

    void setInitialState(const HardState& hs, const ConfState& cs) {
        hardState_ = hs;
        confState_ = cs;
    }

    InitialState initialState() override { return {hardState_, confState_}; }

    std::vector<Entry> entries(Index lo, Index hi, uint64_t) override {
        if (lo < firstIndex()) throw std::out_of_range("compacted");
        std::vector<Entry> r;
        for (Index i = lo; i < std::min(hi, lastIndex() + 1); ++i) r.push_back(entries_[i]);
        return r;
    }

    Term term(Index i) override {
        if (i < firstIndex() || i > lastIndex()) throw std::out_of_range("term");
        return entries_[i].term;
    }

    Index firstIndex() override { return snapIndex_ + 1; }
    Index lastIndex() override  { return static_cast<Index>(entries_.size()) - 1; }

    Snapshot snapshot() override {
        Snapshot s; s.index = snapIndex_; s.term = snapTerm_; return s;
    }

    // Write side (test-only)
    void append(const std::vector<Entry>& ents) {
        if (ents.empty()) return;
        Index first = ents[0].index;
        // Truncate any existing entries from 'first' onward (etcd pattern).
        if (first <= lastIndex()) {
            entries_.resize(first);
        }
        for (const auto& e : ents) {
            entries_.push_back(e);
        }
    }
    void applySnapshot(Index i, Term t) { snapIndex_ = i; snapTerm_ = t; }

private:
    std::vector<Entry> entries_;
    HardState hardState_{};
    ConfState confState_{};
    Index snapIndex_ = 0;
    Term  snapTerm_  = 0;
};

// ============================================================
// ClusterHarness: N nodes in-process, deterministic, chaos-capable
// ============================================================

class ClusterHarness {
public:
    using DropFilter = std::function<bool(NodeId from, NodeId to)>;
    using ApplyCallback = std::function<void(NodeId, const std::vector<Entry>&)>;

    ClusterHarness(std::vector<NodeId> peers,
                  uint32_t electionTick = 10,
                  uint32_t heartbeatTick = 1);

    // Step all nodes by 'n' ticks, delivering messages between each tick.
    void step(uint32_t n = 1);

    // Deliver all queued messages (applying the drop filter).
    void deliver();

    // Propose on a specific node. Returns false if not leader.
    bool propose(NodeId id, const std::vector<uint8_t>& data);

    // Access a node.
    RawNode& node(NodeId id);
    MemoryStorage& storage(NodeId id);

    // Chaos controls.
    void setDropFilter(DropFilter f) { dropFilter_ = std::move(f); }
    void clearDropFilter() { dropFilter_ = nullptr; }

    // Apply callback: called with committed entries for each node during
    // step(). Lets tests attach FSMs that apply entries in real time.
    void setApplyCallback(ApplyCallback cb) { applyCallback_ = std::move(cb); }

    // Find the current leader (or 0 if none).
    NodeId leader() const;

private:
    void send(const Message& m);  // queue instead of socket

    std::vector<NodeId>                          peerIds_;
    uint32_t                                     electionTick_;
    uint32_t                                     heartbeatTick_;
    DropFilter                                   dropFilter_;
    ApplyCallback                                applyCallback_;

    std::map<NodeId, std::unique_ptr<RawNode>>      nodes_;
    std::map<NodeId, std::unique_ptr<MemoryStorage>> storages_;
    std::queue<Message>                            msgQueue_;
};

} // namespace test
} // namespace raft
