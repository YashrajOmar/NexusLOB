#include "raft/RaftLog.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace raft {

namespace {
constexpr uint64_t noLimit = std::numeric_limits<uint64_t>::max();
}

RaftLog::RaftLog(Storage* storage)
    : storage_(storage)
    , committed_(storage ? storage->firstIndex() - 1 : 0)
    , applied_(storage ? storage->firstIndex() - 1 : 0) {

    // If a snapshot exists, committed_ and applied_ must be at least
    // at the snapshot index — those entries are already applied.
    if (storage) {
        auto snap = storage->snapshot();
        if (snap.index > committed_) {
            committed_ = snap.index;
            applied_   = snap.index;
        }
    }
}

// --- Term / Index queries ---

Term RaftLog::term(Index i) const {
    // Dummy entry at the front of the log (before firstIndex).
    if (i == firstIndex() - 1) {
        return 0;
    }
    // Check unstable first (handles snapshot + in-memory entries).
    auto ut = unstable_.maybeTerm(i);
    if (ut.has_value()) {
        return *ut;
    }
    // Check storage for persisted entries.
    try {
        return storage_->term(i);
    } catch (const std::exception&) {
        return 0;
    }
}

Index RaftLog::firstIndex() const {
    auto ui = unstable_.maybeFirstIndex();
    if (ui.has_value()) {
        return *ui;
    }
    return storage_->firstIndex();
}

Index RaftLog::lastIndex() const {
    auto ui = unstable_.maybeLastIndex();
    if (ui.has_value()) {
        return *ui;
    }
    return storage_->lastIndex();
}

Term RaftLog::lastTerm() const {
    return term(lastIndex());
}

// --- Commit / Apply tracking ---

Index RaftLog::commitIndex() const { return committed_; }
Index RaftLog::appliedIndex() const { return applied_; }

bool RaftLog::maybeCommit(Index index, Term t) {
    // Figure 8: only commit if the entry at 'index' is from term 't'.
    // This prevents committing entries from previous terms without
    // a current-term entry also being committed.
    if (index > committed_ && term(index) == t) {
        committed_ = index;
        return true;
    }
    return false;
}

void RaftLog::commitTo(Index index) {
    // Follower side: trust the leader's commitIndex. No term check —
    // the leader already verified safety before sending this value.
    if (index > committed_) {
        Index ci = std::min(index, lastIndex());
        committed_ = ci;
    }
}

std::vector<Entry> RaftLog::nextCommittedEntries(bool allowConfChange) const {
    if (committed_ <= applied_) {
        return {};
    }
    std::vector<Entry> ents;
    try {
        ents = slice(applied_ + 1, committed_ + 1, noLimit);
    } catch (const std::exception&) {
        return {};
    }
    if (ents.empty()) {
        return {};
    }
    if (!allowConfChange) {
        // Only one conf change at a time: cut off at the first one found.
        for (size_t i = 0; i < ents.size(); ++i) {
            if (ents[i].type == EntryType::ConfChange ||
                ents[i].type == EntryType::ConfChangeV2) {
                ents.resize(i);
                break;
            }
        }
    }
    return ents;
}

void RaftLog::appliedTo(Index i) {
    if (i < applied_ || i > committed_) {
        return;
    }
    applied_ = i;
}

// --- Follower append (Section 5.3) ---

bool RaftLog::maybeAppend(Index prevLogIndex, Term prevLogTerm,
                          Index leaderCommit,
                          const std::vector<Entry>& entries) {
    // Check prevLogIndex/prevLogTerm match (Log Matching Property).
    if (term(prevLogIndex) != prevLogTerm) {
        return false;
    }

    Index lastNewIndex = prevLogIndex + entries.size();
    Index conflict = findConflict(entries);

    if (conflict == 0) {
        // No conflict, nothing to append.
    } else if (conflict <= committed_) {
        // Conflict below committed. A correct leader never sends entries
        // that conflict with committed entries. If this happens, it means
        // the follower's committed_ was advanced (e.g. by a heartbeat) past
        // entries it hasn't actually received from this leader. Allow the
        // overwrite — the leader's log is authoritative.
    } else {
        // Truncate from the conflict point and append the rest.
        // The conflicting entry is at position (conflict - prevLogIndex - 1)
        // in the incoming entries vector.
        std::vector<Entry> toAppend(
            entries.begin() + static_cast<std::ptrdiff_t>(conflict - prevLogIndex - 1),
            entries.end());
        unstable_.append(toAppend);
    }

    // Advance commit index to min(leaderCommit, lastNewIndex).
    // Paper Figure 3: "If leaderCommit > commitIndex, set commitIndex =
    // min(leaderCommit, index of last new entry)."
    if (leaderCommit > committed_) {
        committed_ = std::min(leaderCommit, lastNewIndex);
    }

    return true;
}

// --- Leader append ---

Index RaftLog::append(const std::vector<Entry>& entries) {
    if (entries.empty()) {
        return lastIndex();
    }
    Index after = entries.front().index - 1;
    if (after < committed_) {
        // Can't append before the commit index — a bug upstream.
        return lastIndex();
    }
    unstable_.append(entries);
    return lastIndex();
}

// --- Snapshot (Section 7) ---

void RaftLog::restore(const Snapshot& snap) {
    committed_ = snap.index;
    applied_ = snap.index;
    unstable_.restore(snap);
}

Index RaftLog::snapshotIndex() const {
    auto s = unstable_.snapshot();
    if (s.has_value()) {
        return s->index;
    }
    return storage_->snapshot().index;
}

Term RaftLog::snapshotTerm() const {
    auto s = unstable_.snapshot();
    if (s.has_value()) {
        return s->term;
    }
    return storage_->snapshot().term;
}

Snapshot RaftLog::snapshot() const {
    auto s = unstable_.snapshot();
    if (s.has_value()) {
        return *s;
    }
    return storage_->snapshot();
}

// --- Persistence surface ---

std::vector<Entry> RaftLog::unstableEntries() const {
    return unstable_.entries();
}

void RaftLog::stableTo(Index i, Term t) {
    unstable_.stableTo(i, t);
}

void RaftLog::stableSnapTo(Index i) {
    unstable_.stableSnapTo(i);
}

// --- Private helpers ---

Index RaftLog::findConflict(const std::vector<Entry>& entries) const {
    for (const auto& e : entries) {
        if (term(e.index) != e.term) {
            return e.index;
        }
    }
    return 0;
}

std::vector<Entry> RaftLog::slice(Index lo, Index hi, uint64_t maxSize) const {
    if (lo >= hi) {
        return {};
    }
    if (lo < firstIndex()) {
        throw std::out_of_range("slice below first index");
    }
    if (hi > lastIndex() + 1) {
        throw std::out_of_range("slice above last index");
    }

    std::vector<Entry> ents;
    Index unstableOffset = unstable_.offset();

    // Get entries from storage [lo, min(hi, unstableOffset)).
    if (lo < unstableOffset) {
        Index storageHi = std::min(hi, unstableOffset);
        auto stored = storage_->entries(lo, storageHi, maxSize);
        ents = std::move(stored);
        // If storage returned fewer entries (hit maxSize), return early.
        if (ents.size() < storageHi - lo) {
            return ents;
        }
    }

    // Get entries from unstable [max(lo, unstableOffset), hi).
    if (hi > unstableOffset) {
        Index unstableLo = std::max(lo, unstableOffset);
        auto unstable = unstable_.slice(unstableLo, hi);
        if (!ents.empty()) {
            ents.insert(ents.end(), unstable.begin(), unstable.end());
        } else {
            ents = std::move(unstable);
        }
    }

    // Truncate to maxSize (keep at least one entry).
    if (maxSize != noLimit && ents.size() > 1) {
        uint64_t size = ents[0].data.size() + sizeof(Entry);
        for (size_t i = 1; i < ents.size(); ++i) {
            size += ents[i].data.size() + sizeof(Entry);
            if (size > maxSize) {
                ents.resize(i);
                break;
            }
        }
    }

    return ents;
}

} // namespace raft
