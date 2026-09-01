#pragma once

#include "Types.h"
#include "RPC.h"
#include "Ready.h"
#include "RaftLog.h"
#include "ReadOnly.h"
#include "tracker/Progress.h"

#include <vector>
#include <map>
#include <cstdint>

namespace raft {

// Config: tunable parameters. Mirrors etcd's raft.Config.
struct Config {
    NodeId    id               = 0;
    NodeId    leaderId         = 0;       // transient hint, not persisted
    uint32_t  electionTick     = 10;      // randomized in [electionTick, 2*electionTick)
    uint32_t  heartbeatTick    = 1;
    Storage*  storage          = nullptr;
    uint64_t  maxSizePerMsg    = 1ull << 20;   // 1 MiB, bounds AppendEntries size
    uint32_t  maxInflightMsgs   = 256;          // pipeline depth per peer
    std::vector<NodeId> peers;        // bootstrap only — used if log is empty
    std::vector<NodeId> learners;     // bootstrap only
};

// RawNode: the poll-based raft state machine. Single-threaded by contract —
// the application owns serialization (see app/Server).
//
// Usage loop (matches etcd's Ready contract):
//   while running:
//     if node.tick():              // call on each tick interval
//     while msg = recv(): node.step(msg)
//     if node.hasReady():
//         rd = node.poll()
//         persist(rd.entries, rd.hardState, rd.snapshot)
//         send(rd.messages)
//         apply(rd.committedEntries)
//         node.advance()
class RawNode {
public:
    explicit RawNode(const Config& c);
    ~RawNode();

    // --- Driving the state machine ---

    // Advance the logical clock by one tick. Returns true if the tick
    // triggered a timeout (election timeout or heartbeat due).
    bool tick();

    // Feed an inbound message (RPC from a peer, or an internal signal).
    void step(const Message& m);

    // Propose a command to be replicated. Only valid on a leader.
    // Returns false if this node is not the leader.
    bool propose(const std::vector<uint8_t>& data);

    // --- Ready protocol ---

    // True if there is work pending (entries to persist, messages to send,
    // or committed entries to apply).
    bool hasReady() const;

    // Snapshot the pending work into a Ready. Does not clear state until
    // advance() is called.
    Ready poll();

    // Acknowledge that the last Ready was processed. Clears the emitted
    // entries/messages and advances the log's applied marker.
    void advance();

    // --- Observability ---

    Role   role() const;
    Term   term() const;
    NodeId lead() const;
    NodeId id()   const;

    // Per-peer replication progress (leader only).
    const std::map<NodeId, tracker::Progress>& progress() const;

    // Last log index (for tracking proposal indices).
    Index lastIndex() const;

    // Term of the entry at index i (checks unstable + storage).
    Term entryTerm(Index i) const;

private:
    void becomeFollower(Term term, NodeId lead);
    void becomeCandidate();
    void becomePreCandidate();
    void becomeLeader();

    void stepSlow(const Message& m);   // helpers dispatched by step()
    void stepLeader(const Message& m);
    void stepCandidate(const Message& m);
    void stepFollower(const Message& m);

    void tickHeartbeat();   // leader tick
    bool tickElection();   // follower/candidate tick

    void send(const Message& m);     // queue into outgoing buffer
    void collectMessages(Ready& rd); // drain the buffer into Ready

    // election + replication helpers
    void campaign();
    void pollVote(NodeId from, bool granted);
    void broadcastAppend();
    void sendAppend(NodeId to);
    void broadcastHeartbeat();
    void sendHeartbeat(NodeId to);
    void sendSnapshot(NodeId to);
    bool maybeCommit();
    void restore(const Snapshot& snap);
    std::size_t quorum() const;

    // --- identity / election state ---
    NodeId    id_;
    NodeId    lead_   = 0;
    Term      term_   = 0;
    NodeId    vote_   = 0;          // votedFor
    Role      role_   = Role::Follower;

    // --- timing ---
    uint32_t  electionTick_;
    uint32_t  heartbeatTick_;
    uint32_t  randomizedElectionTick_ = 0;
    uint32_t  tickCount_ = 0;

    // --- election vote tracking (candidate only) ---
    std::vector<NodeId> votes_;

    // --- log + replication ---
    RaftLog                       log_;
    std::map<NodeId, tracker::Progress>    pr_;      // per-peer tracker (leader)
    ReadOnly                      readOnly_{ReadOnlyOption::Safe};

    // --- cluster membership (for quorum) ---
    std::vector<NodeId> voters_;
    std::vector<NodeId> learners_;

    // --- outgoing message buffer (drained into Ready.messages) ---
    std::vector<Message>         msgs_;

    // --- advance() bookkeeping ---
    // The last poll()'s entries, so advance() can tell RaftLog they're persisted.
    Index lastReadyLastIndex_ = 0;
    bool  prevHardStateEmpty_  = true;
    Index lastReadyCommit_     = 0;     // last poll()'s commitIndex
    Index lastReadyApplied_    = 0;     // last poll()'s appliedIndex (after apply)

    // --- config limits ---
    uint64_t maxSizePerMsg_;
    uint32_t maxInflightMsgs_;
};

} // namespace raft
