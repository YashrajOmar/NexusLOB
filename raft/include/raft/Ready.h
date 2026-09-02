#pragma once

#include "Types.h"
#include "Storage.h"   // for HardState
#include "RPC.h"       // for Message, Snapshot
#include "ReadOnly.h"  // for ReadState

#include <vector>
#include <optional>

namespace raft {

// Ready: the bundle of work the application must process before calling
// RawNode::advance(). Returned by RawNode::poll().
//
// Processing order (etcd's contract):
//   1. Persist Entries, HardState, and Snapshot to stable storage.
//   2. Send Messages to peers.
//   3. Apply CommittedEntries to the FSM.
//   4. Call advance() to tell RawNode you're done.
struct Ready {
    // Entries the leader/follower has appended but not yet persisted.
    std::vector<Entry> entries;

    // Persistent state to write (currentTerm, votedFor, commitIndex).
    // Empty fields mean "no change"; check soft.HardState != prevHardState
    // via RawNode's tracking instead of inspecting this directly.
    HardState hardState;

    // Outgoing RPCs: MsgApp, MsgVote, MsgHeartbeat, MsgSnap, etc.
    // MUST NOT be sent before the HardState above is persisted.
    std::vector<Message> messages;

    // Entries in (applied, committed] that the FSM should apply.
    // Appears in log order; applying them in any other order is a bug.
    std::vector<Entry> committedEntries;

    // Snapshot to apply to the FSM (InstallSnapshot received).
    // If non-empty, apply this before any committedEntries.
    std::optional<Snapshot> snapshot;

    // ReadStates from ReadIndex/LeaseBased reads. The app should wait
    // until appliedIndex >= ReadState.index, then serve the read.
    std::vector<ReadState> readStates;

    // True if any entries were truncated relative to the last Ready.
    bool entriesRemoved = false;
};

} // namespace raft
