#pragma once

#include <cstdint>
#include <vector>

namespace raft {

// Term: consecutive integer acting as a logical clock.
using Term = uint64_t;

// Index of a log entry.
using Index = uint64_t;

// Server ID.
using NodeId = uint64_t;

enum class EntryType : uint8_t {
    Normal,        // command for the state machine
    ConfChange,    // configuration change via single-node add/remove
    ConfChangeV2,  // configuration change via joint consensus
};

// Role of a server (paper Section 3.4, Figure 4).
enum class Role : uint8_t {
    Follower,
    Candidate,
    Leader,
    PreCandidate,   // PreVote phase (thesis Section 9.6)
};

// Log entry: a term number and a command for the state machine.
struct Entry {
    Term      term = 0;
    Index     index = 0;
    EntryType type = EntryType::Normal;
    std::vector<uint8_t> data;
};

} // namespace raft
