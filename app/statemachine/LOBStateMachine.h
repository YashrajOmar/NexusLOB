#pragma once

#include "statemachine/StateMachine.h"
#include "statemachine/IOrderBook.h"
#include "protocol/CommandCodec.h"

#include <mutex>
#include <memory>
#include <cstdint>

namespace app {

// LOBStateMachine: a limit-order-book matching engine that implements
// the replicated StateMachine interface AND BookReader for reads.
//
// The matching engine is injected via IOrderBook — the FSM doesn't know
// whether it's backed by std::map (MapOrderBook) or a skip list
// (SkipListOrderBook). This is pure dependency injection: the caller
// picks the implementation at construction time.
//
// The raft algorithm calls apply() for each committed log entry, in order.
// apply() decodes the binary order command (NEW/CXL/MOD), executes it on
// the injected IOrderBook, and returns the encoded result (fills, status).
//
// A monotonic sequence counter (nextSeq_) is incremented per apply() call.
// This guarantees every replica assigns the same sequence to the same
// log entry, making matching deterministic across nodes.
class LOBStateMachine : public StateMachine, public BookReader {
public:
    // Inject the order book implementation. Caller chooses MapOrderBook
    // or SkipListOrderBook. Pass nullptr to default to MapOrderBook.
    explicit LOBStateMachine(std::unique_ptr<IOrderBook> book = nullptr);

    // --- StateMachine interface ---
    std::vector<uint8_t> apply(const std::vector<uint8_t>& data) override;
    std::vector<uint8_t> snapshot() const override;
    void restore(const std::vector<uint8_t>& data) override;

    // --- Direct reads (BookReader interface) ---
    std::vector<PriceLevel>      bids(size_t maxLevels = 0) const override;
    std::vector<PriceLevel>      asks(size_t maxLevels = 0) const override;
    std::optional<int64_t>       bestBid() const;
    std::optional<int64_t>       bestAsk() const;
    size_t                       orderCount() const;
    const std::string&           symbol() const;

private:
    mutable std::mutex        mutex_;
    std::unique_ptr<IOrderBook> book_;
    uint64_t                   nextSeq_ = 1;
};

} // namespace app
