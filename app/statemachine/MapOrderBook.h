#pragma once

#include "statemachine/IOrderBook.h"

#include <map>
#include <list>
#include <unordered_map>

namespace app {

// MapOrderBook: a price-time-priority L3 matching engine backed by std::map.
//
// Bids use std::greater (highest price first = best bid).
// Asks use default std::less (lowest price first = best ask).
// Each price level holds a std::list<Order> for FIFO matching.
// An unordered_map gives O(1) cancel/modify by orderId.
//
// Pure data structure — no I/O, no threads. Unit-testable in isolation.
class MapOrderBook : public IOrderBook {
public:
    explicit MapOrderBook(std::string symbol = "");

    std::vector<Fill> newOrder(uint64_t orderId, Side side,
                               int64_t price, int64_t quantity,
                               uint64_t sequence) override;
    bool cancelOrder(uint64_t orderId) override;
    bool modifyOrder(uint64_t orderId, int64_t newPrice,
                     int64_t newQuantity, uint64_t newSequence) override;

    const Order* getOrder(uint64_t orderId) const override;
    size_t       orderCount() const override;
    const std::string& symbol() const override;

    std::vector<PriceLevel>      bids(size_t maxLevels = 0) const override;
    std::vector<PriceLevel>      asks(size_t maxLevels = 0) const override;
    std::optional<int64_t>       bestBid() const override;
    std::optional<int64_t>       bestAsk() const override;

    void addRestingOrder(const Order& o) override;
    std::vector<Order> allOrders() const override;
    void reset(std::string symbol) override;

private:
    std::string symbol_;

    std::map<int64_t, std::list<Order>, std::greater<int64_t>> bids_;
    std::map<int64_t, std::list<Order>> asks_;

    struct OrderLocation {
        Side                       side;
        int64_t                    price;
        std::list<Order>::iterator it;
    };
    std::unordered_map<uint64_t, OrderLocation> orderIndex_;

    std::vector<Fill> match(Side takerSide, int64_t takerPrice,
                            int64_t& remainingQty, uint64_t takerId);
    void addResting(const Order& o);
};

} // namespace app
