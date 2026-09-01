#include "raft/tracker/Progress.h"

#include <algorithm>
#include <cstdlib>

namespace raft {
namespace tracker {

// --- Inflights: ring buffer of in-flight MsgApp indices ---

Inflights::Inflights(std::size_t capacity)
    : buffer_(capacity, 0) {}

bool Inflights::full() const {
    return count_ == buffer_.size();
}

void Inflights::add(Index index) {
    if (full()) {
        // Caller (Progress::update path) must check full() before adding.
        // In production etcd panics here; we do the same via abort to surface bugs.
        std::abort();
    }
    std::size_t next = head_ + count_;
    if (next >= buffer_.size()) {
        next -= buffer_.size();
    }
    buffer_[next] = index;
    ++count_;
}

void Inflights::freeTo(Index to) {
    if (count_ == 0) {
        return;
    }
    std::size_t i   = 0;
    std::size_t idx = head_;
    while (i < count_) {
        if (to < buffer_[idx]) {
            break;
        }
        ++idx;
        if (idx >= buffer_.size()) {
            idx -= buffer_.size();
        }
        ++i;
    }
    count_ -= i;
    head_ = (count_ == 0) ? 0 : idx;
}

void Inflights::reset() {
    count_ = 0;
    head_  = 0;
}

// --- Progress: per-peer replication tracker ---

Progress::Progress(std::size_t inflightCapacity, bool learner)
    : isLearner(learner), inflights(inflightCapacity) {}

bool Progress::isCaughtUp() const {
    return matchIndex + 1 == nextIndex;
}

void Progress::reset(Index lastIndex) {
    matchIndex = 0;
    nextIndex  = lastIndex + 1;
    state      = ProgressState::Probe;
    paused     = false;
    pendingSnapshotIndex = 0;
    inflights.reset();
}

void Progress::update(Index lastIndex) {
    matchIndex = std::max(matchIndex, lastIndex);
    if (state == ProgressState::Replicate) {
        nextIndex = lastIndex + 1;
        inflights.freeTo(lastIndex);
    }
}

bool Progress::maybeDecrFromRejectHint(Index rejected, Index hint) {
    if (state == ProgressState::Replicate) {
        // The rejection must be stale (index <= matchIndex) to ignore.
        if (rejected <= matchIndex) {
            return false;
        }
        // Otherwise reset nextIndex to matchIndex + 1 and re-probe.
        nextIndex = matchIndex + 1;
        return true;
    }

    // Probe state: ignore stale rejections.
    if (rejected < nextIndex - 1) {
        return false;
    }
    // Decrement to the rejected prevLogIndex (paper §5.3: decrement and retry).
    // The hint (follower's lastIndex) helps when the follower's log is shorter
    // than prevLogIndex — in that case jump to hint+1. Otherwise back up by 1.
    if (hint < rejected) {
        // Follower's log is shorter than prevLogIndex. Jump to hint + 1
        // so we send starting from the entry after the follower's last.
        nextIndex = hint + 1;
    } else {
        // Follower has prevLogIndex but with wrong term. Decrement by 1
        // and retry until we find a matching point.
        nextIndex = rejected;
    }
    if (nextIndex < 1) {
        nextIndex = 1;
    }
    return true;
}

void Progress::becomeSnapshot(Index snapshotIndex) {
    state = ProgressState::Snapshot;
    pendingSnapshotIndex = snapshotIndex;
    paused = true;
}

void Progress::becomeProbe() {
    // Drop out of Snapshot state.
    if (state == ProgressState::Snapshot) {
        pendingSnapshotIndex = 0;
    }
    state = ProgressState::Probe;
    paused = false;
}

void Progress::becomeReplicate() {
    state = ProgressState::Replicate;
    paused = false;
    nextIndex = matchIndex + 1;
    inflights.reset();
}

} // namespace tracker
} // namespace raft
