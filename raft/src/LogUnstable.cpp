#include "raft/LogUnstable.h"

#include <algorithm>

namespace raft {

LogUnstable::LogUnstable() = default;

std::optional<Index> LogUnstable::maybeFirstIndex() const {
    if (snapshot_.has_value()) {
        return snapshot_->index + 1;
    }
    return std::nullopt;
}

std::optional<Index> LogUnstable::maybeLastIndex() const {
    if (!entries_.empty()) {
        return offset_ + entries_.size() - 1;
    }
    if (snapshot_.has_value()) {
        return snapshot_->index;
    }
    return std::nullopt;
}

std::optional<Term> LogUnstable::maybeTerm(Index i) const {
    // Below the entries window: only the snapshot can answer.
    if (i < offset_) {
        if (snapshot_.has_value() && snapshot_->index == i) {
            return snapshot_->term;
        }
        return std::nullopt;
    }
    auto last = maybeLastIndex();
    if (!last.has_value() || i > *last) {
        return std::nullopt;
    }
    return entries_[i - offset_].term;
}

void LogUnstable::stableTo(Index i, Term t) {
    if (snapshot_.has_value()) {
        // App confirms the snapshot itself is persisted.
        if (i == snapshot_->index && t == snapshot_->term) {
            snapshot_.reset();
            return;
        }
        // Anything at or below the snapshot index is not in unstable.
        if (i <= snapshot_->index) {
            return;
        }
    }
    if (entries_.empty() || i < offset_) {
        return;
    }
    Index drop = i + 1 - offset_;
    if (drop >= entries_.size()) {
        entries_.clear();
    } else {
        entries_.erase(entries_.begin(),
                       entries_.begin() + static_cast<std::ptrdiff_t>(drop));
    }
    offset_ = i + 1;
}

void LogUnstable::stableSnapTo(Index i) {
    if (snapshot_.has_value() && snapshot_->index == i) {
        snapshot_.reset();
    }
}

void LogUnstable::restore(const Snapshot& snap) {
    entries_.clear();
    snapshot_ = snap;
    offset_ = snap.index + 1;
}

void LogUnstable::truncateFrom(Index fromIndex) {
    if (entries_.empty()) return;
    if (fromIndex < offset_) {
        entries_.clear();
        return;
    }
    Index cut = fromIndex - offset_;
    if (cut < entries_.size()) {
        entries_.resize(cut);
    }
}

void LogUnstable::append(const std::vector<Entry>& entries) {
    truncateAndAppend(entries);
}

void LogUnstable::truncateAndAppend(const std::vector<Entry>& entries) {
    if (entries.empty()) {
        return;
    }
    Index first = entries.front().index;

    if (entries_.empty()) {
        offset_ = first;
        entries_ = entries;
        return;
    }

    Index ourLast = offset_ + entries_.size() - 1;

    // New slice starts at or before our window: replace entirely.
    if (first <= offset_) {
        offset_ = first;
        entries_ = entries;
        return;
    }

    // Gap above our window: defensive replace (a correct Raft never produces
    // a gap; this guards against a bug upstream).
    if (first > ourLast + 1) {
        offset_ = first;
        entries_ = entries;
        return;
    }

    // Overlap or contiguous: drop our tail from `first` onward, then append.
    Index cut = first - offset_;
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(cut),
                   entries_.end());
    entries_.insert(entries_.end(), entries.begin(), entries.end());
}

std::vector<Entry> LogUnstable::slice(Index lo, Index hi) const {
    if (lo < offset_ || hi > offset_ + entries_.size() || lo > hi) {
        return {};
    }
    auto begin = entries_.begin() + static_cast<std::ptrdiff_t>(lo - offset_);
    auto end   = entries_.begin() + static_cast<std::ptrdiff_t>(hi - offset_);
    return std::vector<Entry>(begin, end);
}

} // namespace raft
