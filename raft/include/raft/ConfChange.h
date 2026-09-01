#pragma once

#include "Types.h"
#include "Storage.h"   // for ConfState

#include <vector>
#include <cstdint>

namespace raft {

// ConfChangeType: a single-node membership operation (legacy ConfChange API).
// Paper Section 6 classic variant: add or remove one server at a time.
enum class ConfChangeType : uint8_t {
    AddNode,         // add a voting member
    AddLearnerNode,  // add a non-voting learner (catches up before promotion)
    RemoveNode,      // remove a member or learner
};

// ConfChangeTransition: how to move between configurations (ConfChangeV2 API).
// Joint consensus default; explicit modes for edge cases.
enum class ConfChangeTransition : uint8_t {
    // Automatically decide: if the change is a single-node add/remove, apply it
    // directly (the simple case the paper calls out); otherwise use joint consensus.
    Auto,
    // Force joint consensus even for a single-node change.
    JointConsensus,
    // Skip the joint phase and go directly to the new config. Unsafe in general;
    // only valid when the change preserves one common voter across old and new.
    Explicit,
};

// ConfChange: legacy single-node membership change.
// Applied as one log entry of type EntryType::ConfChange.
struct ConfChange {
    ConfChangeType type = ConfChangeType::AddNode;
    NodeId         nodeID = 0;
    std::vector<uint8_t> context;   // opaque, e.g. peer address for bootstrap
};

// ConfChangeV2: joint-consensus membership change.
// Applied as one log entry of type EntryType::ConfChangeV2.
// Carries a batch of changes and a transition mode.
struct ConfChangeV2 {
    ConfChangeTransition              transition = ConfChangeTransition::Auto;
    std::vector<ConfChange>           changes;
};

// Assemble the voters/learners from a list of ConfChanges into a ConfState.
// Used by RawNode when applying a committed ConfChange entry.
ConfState applyConfChanges(const ConfState& base,
                           const std::vector<ConfChange>& changes);

} // namespace raft
