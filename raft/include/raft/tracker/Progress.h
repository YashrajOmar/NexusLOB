#pragma once

#include "raft/Types.h"

#include <vector>
#include <cstdint>
#include <deque>

namespace raft {
namespace tracker {

// ProgressState: the replication state of a peer (etcd's ProgressStateType).
enum class ProgressState : uint8_t {
    // Probe: leader sends one MsgApp at a time, waiting for a response before
    // sending the next. Used when the follower is being repaired or has fallen
    // far behind (matchIndex + 1 < nextIndex).
    Probe,
    // Replicate: leader pipelines MsgApp up to maxInflightMsgs. Normal steady
    // state for a healthy, caught-up follower.
    Replicate,
    // Snapshot: leader is sending MsgSnap instead of MsgApp. Paused for
    // regular log replication until the snapshot is acknowledged.
    Snapshot,
};

// Inflights: a ring buffer tracking the indices of in-flight MsgApp payloads
// sent to a peer but not yet acknowledged. Caps at 'capacity' entries; leader
// must wait for an ack before sending more once full (flow control, thesis 10.2.1).
class Inflights {
public:
    explicit Inflights(std::size_t capacity = 256);

    bool full() const;
    void add(Index index);
    // Free up all in-flight entries up to and including 'to', matching an ack.
    void freeTo(Index to);
    void reset();

    std::size_t count()    const { return count_; }
    std::size_t capacity() const { return buffer_.size(); }

private:
    std::vector<Index> buffer_;
    std::size_t        head_  = 0;   // oldest in-flight index slot
    std::size_t        count_ = 0;   // number of valid slots
};

// Progress: per-peer replication tracker. Maintained by the leader only.
// Followers don't track their peers.
//
// Fields map to the paper's per-follower state (Section 3.4) plus etcd's
// flow-control and snapshot extensions.
struct Progress {
    // Highest log index known to be replicated on this peer.
    // Used to compute commitIndex (majority of matchIndex values).
    Index matchIndex = 0;

    // Next log index to send to this peer. Initialized to leader.lastIndex + 1
    // on election; decremented on AppendEntries rejection.
    Index nextIndex  = 0;

    // Current replication state (Probe / Replicate / Snapshot).
    ProgressState state = ProgressState::Probe;

    // Paused: in Probe state, the leader sends one MsgApp and waits. While
    // waiting, paused = true prevents further sends.
    bool paused = false;

    // Index of the snapshot being sent (only meaningful in Snapshot state).
    Index pendingSnapshotIndex = 0;

    // True if this peer is a learner (non-voting, catching up).
    bool isLearner = false;

    // In-flight MsgApp tracking (flow control).
    Inflights inflights;

    Progress() = default;
    Progress(std::size_t inflightCapacity, bool learner = false);

    // True if the peer is caught up (matchIndex + 1 == nextIndex).
    bool isCaughtUp() const;

    // Reset to initial state for a new leader term.
    void reset(Index lastIndex);

    // Update on a successful AppendEntries ack: advance matchIndex and
    // free the corresponding in-flight slots.
    void update(Index lastIndex);

    // Update on a rejected AppendEntries: decrement nextIndex using the
    // hint (the follower's last index or the conflicting index).
    // 'rejected' is the index that was rejected; 'hint' is the follower's
    // suggested next index to try (Message::rejectHint).
    bool maybeDecrFromRejectHint(Index rejected, Index hint);

    // Transition into Snapshot state for the given snapshot index.
    void becomeSnapshot(Index snapshotIndex);

    // Transition out of Snapshot state back to Probe (after snap ack).
    void becomeProbe();

    // Transition into Replicate state (after a successful MsgApp in Probe).
    void becomeReplicate();
};

} // namespace tracker
} // namespace raft
