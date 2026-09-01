#pragma once

#include "Types.h"
#include "RPC.h"   // for Snapshot

#include <vector>
#include <optional>

namespace raft {

// LogUnstable: in-memory log entries and snapshot not yet written to Storage.
// etcd's log_unstable.go. Entries live here between the moment the algorithm
// appends them and the moment the application confirms persistence (stableTo).
class LogUnstable {
public:
    LogUnstable();

    // First index the unstable view can answer for, or nullopt if it holds
    // nothing and no snapshot. If a snapshot exists, returns snapshot.index + 1.
    std::optional<Index> maybeFirstIndex() const;

    // Last index the unstable view can answer for, or nullopt if empty.
    // If entries is non-empty, returns offset + entries.size() - 1;
    // otherwise returns snapshot.index if a snapshot exists.
    std::optional<Index> maybeLastIndex() const;

    // Term of the entry at index i, or nullopt if out of range.
    std::optional<Term> maybeTerm(Index i) const;

    // Append entries that arrive with indices >= offset. If the incoming slice
    // overlaps the existing tail, the new entries overwrite from the conflict
    // point onward (Log Matching Property, Section 3.4).
    void append(const std::vector<Entry>& entries);

    // Called by the application after entries up to (i, t) are persisted.
    // Drops entries with index <= i. The term disambiguates the snapshot
    // boundary case (etcd's stableTo): if i == snapshot.index and the snapshot
    // term matches t, the snapshot is also considered persisted.
    void stableTo(Index i, Term t);

    // Called by the application after the snapshot at index i is persisted.
    void stableSnapTo(Index i);

    // Replace the unstable view with a freshly received snapshot (Section 7).
    // Clears entries and sets offset to snap.index + 1.
    void restore(const Snapshot& snap);

    // --- accessors used by RaftLog ---

    bool hasSnapshot() const { return snapshot_.has_value(); }
    const std::optional<Snapshot>& snapshot() const { return snapshot_; }
    const std::vector<Entry>& entries() const { return entries_; }
    Index offset() const { return offset_; }

    // Slice [lo, hi) out of the unstable entries (not the snapshot).
    std::vector<Entry> slice(Index lo, Index hi) const;

private:

    // Internal append that handles the offset == 0 (empty) case and the
    // overlap/truncate case.
    void truncateAndAppend(const std::vector<Entry>& entries);

    std::vector<Entry>      entries_;
    Index                   offset_    = 0;   // index of entries_[0] in the log
    std::optional<Snapshot> snapshot_;        // pending un-persisted snapshot
};

} // namespace raft
