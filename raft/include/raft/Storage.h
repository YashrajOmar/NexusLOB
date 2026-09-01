#pragma once

#include "Types.h"
#include "RPC.h"   // for Snapshot
#include <vector>
#include <cstdint>
#include <stdexcept>

namespace raft {

// HardState: persistent state on all servers.
// Paper Section 3.3: saved to stable storage before responding to RPCs.
// commit is etcd's addition (persisted to avoid log scan on restart).
struct HardState {
    Term   term   = 0;  // currentTerm
    NodeId vote   = 0;  // votedFor (0 means none)
    Index  commit = 0;  // commitIndex
};

// ConfState: cluster membership configuration.
struct ConfState {
    std::vector<NodeId> nodes;     // voting members
    std::vector<NodeId> learners;  // non-voting learners
};

// StorageError: conditions raised by Storage methods.
enum class StorageError {
    Compacted,     // requested entries have been compacted away
    Unavailable,   // requested entries are not yet available
    SnapshotOutOfDate,
};

// InitialState: returned by Storage::initialState() at startup.
struct InitialState {
    HardState hardState;
    ConfState confState;
};

// Storage: read-only access to persisted raft state and log.
// The application implements this (e.g., a WriteAheadLog).
// Matches etcd/raft's Storage interface.
class Storage {
public:
    virtual ~Storage() = default;

    // Returns the saved HardState and ConfState at startup.
    virtual InitialState initialState() = 0;

    // Returns log entries in the half-open range [lo, hi),
    // truncated to maxSize bytes. Throws StorageError::Compacted
    // if lo is below the first index.
    virtual std::vector<Entry> entries(Index lo, Index hi, uint64_t maxSize) = 0;

    // Returns the term of the entry at index i.
    // Throws StorageError::Compacted if i is below the first index.
    virtual Term term(Index i) = 0;

    // Index of the first entry in the log (1 if nothing compacted).
    virtual Index firstIndex() = 0;

    // Index of the last entry in the log.
    virtual Index lastIndex() = 0;

    // Most recent snapshot, or an empty Snapshot if none.
    virtual Snapshot snapshot() = 0;
};

} // namespace raft
