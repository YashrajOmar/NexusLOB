#pragma once

#include <cstdint>
#include <list>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

namespace app {

enum class Side : uint8_t { Buy = 0, Sell = 1 };

struct Order {
    uint64_t id        = 0;
    Side     side      = Side::Buy;
    int64_t  price     = 0;
    int64_t  quantity  = 0;
    uint64_t sequence  = 0;
};

struct Fill {
    uint64_t makerOrderId = 0;
    uint64_t takerOrderId = 0;
    int64_t  price         = 0;
    int64_t  quantity      = 0;
    Side     takerSide     = Side::Buy;
};

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

// IOrderBook: abstract interface for a price-time-priority matching engine.
//
// Multiple implementations plug in via dependency injection:
//   - MapOrderBook:    std::map-based (Phase 2)
//   - SkipListOrderBook: skip list-based (Phase 3)
//
// The FSM (LOBStateMachine) receives whichever implementation is injected
// at construction. Server, codec, protocol — none of them change.
class IOrderBook {
public:
    virtual ~IOrderBook() = default;

    // --- Mutations ---
    virtual std::vector<Fill> newOrder(uint64_t orderId, Side side,
                                       int64_t price, int64_t quantity,
                                       uint64_t sequence) = 0;
    virtual bool cancelOrder(uint64_t orderId) = 0;
    virtual bool modifyOrder(uint64_t orderId, int64_t newPrice,
                             int64_t newQuantity, uint64_t newSequence) = 0;

    // --- L3 queries ---
    virtual const Order* getOrder(uint64_t orderId) const = 0;
    virtual size_t       orderCount() const = 0;
    virtual const std::string& symbol() const = 0;

    // --- L2 queries ---
    virtual std::vector<PriceLevel>      bids(size_t maxLevels = 0) const = 0;
    virtual std::vector<PriceLevel>      asks(size_t maxLevels = 0) const = 0;
    virtual std::optional<int64_t>       bestBid() const = 0;
    virtual std::optional<int64_t>       bestAsk() const = 0;

    // --- Snapshot support ---
    virtual void addRestingOrder(const Order& o) = 0;
    virtual std::vector<Order> allOrders() const = 0;

    // Clear all orders and set a new symbol (for snapshot restore).
    virtual void reset(std::string symbol) = 0;
};

} // namespace app
