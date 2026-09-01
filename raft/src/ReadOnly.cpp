#include "raft/ReadOnly.h"

#include <algorithm>

namespace raft {

ReadOnly::ReadOnly(ReadOnlyOption option)
    : option_(option) {}

void ReadOnly::add(Index index, const std::vector<uint8_t>& ctx) {
    if (pending_.count(ctx)) {
        return;  // already pending — ignore duplicate
    }
    pending_[ctx] = index;
    recvQueue_.push_back(ctx);
}

std::optional<Index> ReadOnly::recvAck(const Message& m, std::size_t quorum) {
    const auto& ctx = m.context;
    auto it = pending_.find(ctx);
    if (it == pending_.end()) {
        return std::nullopt;  // unknown context — stale or already resolved
    }

    // Record this peer's ack (dedup: a peer acking twice doesn't count twice).
    auto& ackList = acks_[ctx];
    if (std::find(ackList.begin(), ackList.end(), m.from) == ackList.end()) {
        ackList.push_back(m.from);
    }

    // +1 for the leader itself (implicit self-ack: the leader is in the quorum).
    if (acks_[ctx].size() + 1 >= quorum) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<Index> ReadOnly::advance(const std::vector<uint8_t>& ctx) {
    auto it = pending_.find(ctx);
    if (it == pending_.end()) {
        return std::nullopt;
    }

    Index index = it->second;

    // Pop everything up to and including ctx from the FIFO queue.
    // Earlier requests were registered before ctx; if ctx has quorum,
    // the same followers who acked ctx also acked them (same heartbeat round).
    while (!recvQueue_.empty()) {
        auto front = recvQueue_.front();
        recvQueue_.erase(recvQueue_.begin());
        pending_.erase(front);
        acks_.erase(front);
        if (front == ctx) {
            break;
        }
    }

    return index;
}

void ReadOnly::reset() {
    pending_.clear();
    acks_.clear();
    recvQueue_.clear();
}

} // namespace raft
