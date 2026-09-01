#pragma once

#include "Types.h"
#include "Storage.h"
#include "LogUnstable.h"

#include <vector>

namespace raft {

// RaftLog: the replicated log.
// Composes a persisted view (Storage) with an in-memory view (LogUnstable)
// of entries not yet written to stable storage.
class RaftLog {
public:
    explicit RaftLog(Storage* storage);

    // --- Term / Index queries ---

    // Term of the entry at index i, or 0 if i is out of range.
    Term term(Index i) const;

    // First and last log indices across persisted + unstable.
    Index firstIndex() const;
    Index lastIndex() const;

    // Term of the last log entry.
    Term lastTerm() const;

    // --- Commit / Apply tracking ---

    Index commitIndex() const;
    Index appliedIndex() const;

    // Advance commitIndex. Returns true if it moved.
    bool maybeCommit(Index index, Term term);

    // Set commitIndex directly (follower side: trusts leader's commit).
    // Paper Figure 3: "If leaderCommit > commitIndex, set commitIndex =
    // min(leaderCommit, index of last new entry)."
    void commitTo(Index index);

    // Entries [applied_+1, committed_] to apply to the FSM.
    std::vector<Entry> nextCommittedEntries(bool allowConfChange) const;

    // Mark entries up to i as applied.
    void appliedTo(Index i);

    // --- Follower append (from leader's MsgApp) ---

    // Append entries from a leader. Returns true if prevLogIndex/prevLogTerm
    // matched and entries were accepted.
    bool maybeAppend(Index prevLogIndex, Term prevLogTerm,
                     Index leaderCommit,
                     const std::vector<Entry>& entries);

    // --- Leader append (local proposals) ---

    // Append entries proposed on this node. Returns the last new index.
    Index append(const std::vector<Entry>& entries);

    // --- Snapshot ---

    void restore(const Snapshot& snap);
    Index snapshotIndex() const;
    Term snapshotTerm() const;

    // --- Persistence surface (consumed by Ready) ---

    // Entries not yet persisted to stable storage.
    std::vector<Entry> unstableEntries() const;

    // Mark entries up to (i, t) as persisted.
    void stableTo(Index i, Term t);

    // Mark snapshot at index i as persisted.
    void stableSnapTo(Index i);

    // Slice [lo, hi) from the logical log, bounded by maxSize bytes.
    std::vector<Entry> slice(Index lo, Index hi, uint64_t maxSize) const;

    // Get the current snapshot (term, index, data).
    Snapshot snapshot() const;

private:
    // Find the first index in 'entries' that conflicts with the existing log.
    // Returns 0 if no conflict. A conflict means the entry at entry.index has
    // a different term than the existing log at that index (or the index is
    // beyond the current log).
    Index findConflict(const std::vector<Entry>& entries) const;

    Storage* storage_;       // not owned
    LogUnstable unstable_;

    Index committed_ = 0;
    Index applied_   = 0;
};

} // namespace raft
