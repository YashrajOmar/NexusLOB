#pragma once

#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace app {

// Side of the book.
enum class Side : uint8_t { Buy = 0, Sell = 1 };

// Order: a resting order on the book, or the incoming taker.
//   price    — fixed-point integer (smallest tick); no floats, for
//              deterministic matching across replicas.
//   sequence — priority key within a price level (FIFO). The caller passes
//              the Raft log index so every replica matches identically.
struct Order {
    uint64_t id        = 0;
    Side     side      = Side::Buy;
    int64_t  price     = 0;
    int64_t  quantity  = 0;   // remaining (resting) quantity
    uint64_t sequence  = 0;
};

// Fill: one match of an incoming taker against a resting maker.
//   Trades execute at the maker's price (standard price-time semantics).
struct Fill {
    uint64_t makerOrderId = 0;
    uint64_t takerOrderId = 0;
    int64_t  price         = 0;
    int64_t  quantity      = 0;
    Side     takerSide     = Side::Buy;
};

// PriceLevel: aggregated L2 depth at one price.
struct PriceLevel {
    int64_t  price         = 0;
    int64_t  totalQuantity = 0;
    uint32_t orderCount    = 0;

    bool operator==(const PriceLevel& o) const {
        return price == o.price &&
               totalQuantity == o.totalQuantity &&
               orderCount == o.orderCount;
    }
};

// OrderBook: a price-time-priority L3 matching engine for one symbol.
//
// Pure data structure — no I/O, no threads, no syscalls. Unit-testable in
// isolation; this is where baseline matching latency is measured.
//
// Market orders are expressed as limits at an extreme price: a buy market
// uses INT64_MAX, a sell market uses 0. The book sweeps the opposite side
// until the taker is filled or the book is empty.
class OrderBook {
public:
    explicit OrderBook(std::string symbol = "");

    // --- Mutations ---

    // Submit a new order. Matches against the opposite side first; any
    // unfilled remainder rests on the book. Returns the fills produced.
    // Rejects (returns empty) if quantity <= 0 or orderId already exists.
    // Caller computes remaining = quantity - sum(fills.quantity).
    std::vector<Fill> newOrder(uint64_t orderId, Side side,
                               int64_t price, int64_t quantity,
                               uint64_t sequence);

    // Cancel a resting order by id. Returns true if found and removed.
    bool cancelOrder(uint64_t orderId);

    // Modify a resting order (cancel-replace semantics: the order loses
    // its time priority and is re-queued at the back of its new price
    // level using newSequence). If newQuantity <= 0 the order is just
    // cancelled. Returns true if the original was found.
    bool modifyOrder(uint64_t orderId, int64_t newPrice,
                     int64_t newQuantity, uint64_t newSequence);

    // --- L3 queries ---

    const Order* getOrder(uint64_t orderId) const;
    size_t       orderCount() const;
    const std::string& symbol() const;

    // --- L2 queries (aggregated depth) ---

    // Bids are returned best (highest price) first; asks best (lowest) first.
    // maxLevels == 0 returns all levels.
    std::vector<PriceLevel> bids(size_t maxLevels = 0) const;
    std::vector<PriceLevel> asks(size_t maxLevels = 0) const;

    // Best bid / best ask (BBO). nullopt if that side is empty.
    std::optional<int64_t> bestBid() const;
    std::optional<int64_t> bestAsk() const;

    // --- Snapshot support ---

    // For snapshot restore: add an order directly to the book without
    // matching. The order is placed at the back of its price level's
    // queue. Call in serialized order to preserve FIFO priority.
    void addRestingOrder(const Order& o);

    // For snapshot serialization: return all resting orders in a
    // deterministic order (bids then asks, best price first, FIFO within
    // each level). The same order is used by restore so time priority
    // is preserved across snapshots.
    std::vector<Order> allOrders() const;

private:
    std::string symbol_;

    // Bids ordered best (highest price) first via std::greater.
    std::map<int64_t, std::list<Order>, std::greater<int64_t>> bids_;
    // Asks ordered best (lowest price) first (default ascending).
    std::map<int64_t, std::list<Order>> asks_;

    // O(1) lookup for cancel/modify: orderId -> location in the book.
    struct OrderLocation {
        Side                       side;
        int64_t                    price;
        std::list<Order>::iterator it;
    };
    std::unordered_map<uint64_t, OrderLocation> orderIndex_;

    // Sweep the opposite side for an incoming taker. Reduces remainingQty
    // in place; removes fully-filled makers and empty price levels.
    std::vector<Fill> match(Side takerSide, int64_t takerPrice,
                            int64_t& remainingQty, uint64_t takerId);

    // Insert a resting order onto the correct side and index it.
    void addResting(const Order& o);
};

} // namespace app
