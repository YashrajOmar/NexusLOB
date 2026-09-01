#pragma once

#include "Types.h"
#include "Storage.h"        // for HardState, ConfState
#include "tracker/Progress.h"

#include <map>
#include <cstdint>
#include <optional>

namespace raft {

// SoftState: transient state not persisted. Two nodes with the same HardState
// but different SoftState are both valid (one may be leader now, the other not).
struct SoftState {
    NodeId lead   = 0;       // current leader ID (0 if unknown)
    Role   role   = Role::Follower;
};

// Status: a point-in-time snapshot of a RawNode, for diagnostics.
// Matches etcd's Status struct in status.go.
struct Status {
    // Basic identity / term.
    NodeId id   = 0;
    Term   term = 0;

    // Transient + persistent state.
    SoftState softState;
    HardState hardState;

    // Cluster membership.
    ConfState config;

    // Per-peer replication progress (leader only; empty otherwise).
    std::map<NodeId, tracker::Progress> progress;

    // Index markers (for "how far behind is this node?" questions).
    Index appliedIndex  = 0;
    Index commitIndex   = 0;
    Index lastIndex     = 0;

    // Snapshot metadata.
    std::optional<Index> snapshotIndex;
};

} // namespace raft
