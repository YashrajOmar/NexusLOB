#pragma once

#include "statemachine/StateMachine.h"
#include "statemachine/OrderBook.h"
#include "protocol/CommandCodec.h"

#include <mutex>
#include <cstdint>

namespace app {

// LOBStateMachine: a limit-order-book matching engine that implements
// the replicated StateMachine interface AND BookReader for reads.
//
// The raft algorithm calls apply() for each committed log entry, in order.
// apply() decodes the binary order command (NEW/CXL/MOD), executes it on
// the OrderBook, and returns the encoded result (fills, status).
//
// By implementing BookReader, LOBStateMachine can be injected directly as
// the read interface — Server calls bookReader->bids() without knowing the
// concrete type. No dynamic_cast, no if-else.
//
// A monotonic sequence counter (nextSeq_) is incremented per apply() call.
// This guarantees every replica assigns the same sequence to the same
// log entry, making matching deterministic across nodes.
//
// Thread safety: internal mutex. raft calls apply() serially, but clients
// may call the query methods (bids/asks/BBO) concurrently for direct reads.
class LOBStateMachine : public StateMachine, public BookReader {
public:
    explicit LOBStateMachine(std::string symbol = "");

    // --- StateMachine interface ---

    std::vector<uint8_t> apply(const std::vector<uint8_t>& data) override;
    std::vector<uint8_t> snapshot() const override;
    void restore(const std::vector<uint8_t>& data) override;

    // --- Direct reads (not replicated, not linearizable by themselves) ---
    // Used by Server for fast local reads or as a building block for
    // ReadIndex-based linearizable reads.

    std::vector<PriceLevel>      bids(size_t maxLevels = 0) const;
    std::vector<PriceLevel>      asks(size_t maxLevels = 0) const;
    std::optional<int64_t>       bestBid() const;
    std::optional<int64_t>       bestAsk() const;
    size_t                       orderCount() const;
    const std::string&           symbol() const;

private:
    mutable std::mutex mutex_;
    OrderBook          book_;
    uint64_t           nextSeq_ = 1;
};

} // namespace app
