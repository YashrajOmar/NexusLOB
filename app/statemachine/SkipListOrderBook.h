#pragma once

#include "statemachine/IOrderBook.h"
#include "statemachine/SkipList.h"

#include <list>
#include <unordered_map>
#include <functional>

namespace app {

// SkipListOrderBook: a price-time-priority L3 matching engine backed by
// a custom skip list (SkipList<K,V,Compare>) instead of std::map.
//
// Two skip lists are maintained:
//   - bids_: SkipList<int64_t, std::list<Order>, std::greater<int64_t>>
//     (descending: highest price first = best bid)
//   - asks_: SkipList<int64_t, std::list<Order>, std::less<int64_t>>
//     (ascending: lowest price first = best ask)
//
// Each price level holds a std::list<Order> for FIFO matching.
// An unordered_map gives O(1) cancel/modify by orderId.
//
// Drop-in replacement for MapOrderBook via the IOrderBook interface.
// Pure data structure — no I/O, no threads.
class SkipListOrderBook : public IOrderBook {
public:
    explicit SkipListOrderBook(std::string symbol = "");

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
    using LevelList = std::list<Order>;
    using BidSkipList = SkipList<int64_t, LevelList, std::greater<int64_t>>;
    using AskSkipList = SkipList<int64_t, LevelList, std::less<int64_t>>;

    std::string  symbol_;
    BidSkipList  bids_;
    AskSkipList  asks_;

    struct OrderLocation {
        Side                       side;
        int64_t                    price;
        std::list<Order>::iterator it;
    };
    std::unordered_map<uint64_t, OrderLocation> orderIndex_;

    std::vector<Fill> match(Side takerSide, int64_t takerPrice,
                            int64_t& remainingQty, uint64_t takerId);
    void addResting(const Order& o);

    // Rebuild both skip lists (for reset/restore).
    void rebuild(const std::vector<Order>& orders);
};

} // namespace app
