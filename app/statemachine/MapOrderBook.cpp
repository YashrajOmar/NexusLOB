#include "statemachine/MapOrderBook.h"

#include <algorithm>

namespace app {

MapOrderBook::MapOrderBook(std::string symbol) : symbol_(std::move(symbol)) {}

std::vector<Fill> MapOrderBook::newOrder(uint64_t orderId, Side side,
                                          int64_t price, int64_t quantity,
                                          uint64_t sequence) {
    std::vector<Fill> fills;
    if (quantity <= 0) return fills;
    if (orderIndex_.count(orderId)) return fills;

    int64_t remaining = quantity;
    fills = match(side, price, remaining, orderId);

    if (remaining > 0) {
        Order resting{orderId, side, price, remaining, sequence};
        addResting(resting);
    }
    return fills;
}

bool MapOrderBook::cancelOrder(uint64_t orderId) {
    auto it = orderIndex_.find(orderId);
    if (it == orderIndex_.end()) return false;

    Side     side  = it->second.side;
    int64_t  price = it->second.price;
    auto     listIt = it->second.it;

    if (side == Side::Buy) {
        auto& level = bids_.at(price);
        level.erase(listIt);
        if (level.empty()) bids_.erase(price);
    } else {
        auto& level = asks_.at(price);
        level.erase(listIt);
        if (level.empty()) asks_.erase(price);
    }
    orderIndex_.erase(it);
    return true;
}

bool MapOrderBook::modifyOrder(uint64_t orderId, int64_t newPrice,
                               int64_t newQuantity, uint64_t newSequence) {
    auto it = orderIndex_.find(orderId);
    if (it == orderIndex_.end()) return false;

    Side side = it->second.side;
    cancelOrder(orderId);

    if (newQuantity > 0) {
        Order o{orderId, side, newPrice, newQuantity, newSequence};
        addResting(o);
    }
    return true;
}

std::vector<Fill> MapOrderBook::match(Side takerSide, int64_t takerPrice,
                                       int64_t& remainingQty, uint64_t takerId) {
    std::vector<Fill> fills;

    while (remainingQty > 0) {
        int64_t          bestPrice = 0;
        std::list<Order>* levelPtr = nullptr;

        if (takerSide == Side::Buy) {
            if (asks_.empty()) break;
            auto it = asks_.begin();
            bestPrice = it->first;
            if (takerPrice < bestPrice) break;
            levelPtr = &it->second;
        } else {
            if (bids_.empty()) break;
            auto it = bids_.begin();
            bestPrice = it->first;
            if (takerPrice > bestPrice) break;
            levelPtr = &it->second;
        }

        auto& level = *levelPtr;
        while (remainingQty > 0 && !level.empty()) {
            Order& maker = level.front();
            int64_t matchQty = std::min(remainingQty, maker.quantity);

            Fill f;
            f.makerOrderId = maker.id;
            f.takerOrderId = takerId;
            f.price        = maker.price;
            f.quantity     = matchQty;
            f.takerSide    = takerSide;
            fills.push_back(f);

            remainingQty    -= matchQty;
            maker.quantity  -= matchQty;

            if (maker.quantity <= 0) {
                uint64_t makerId = maker.id;
                orderIndex_.erase(makerId);
                level.pop_front();
            }
        }
        if (level.empty()) {
            if (takerSide == Side::Buy) asks_.erase(bestPrice);
            else                        bids_.erase(bestPrice);
        }
    }
    return fills;
}

void MapOrderBook::addResting(const Order& o) {
    std::list<Order>* levelPtr;
    if (o.side == Side::Buy) levelPtr = &bids_[o.price];
    else                     levelPtr = &asks_[o.price];
    auto& level = *levelPtr;
    level.push_back(o);
    orderIndex_[o.id] = OrderLocation{o.side, o.price, std::prev(level.end())};
}

void MapOrderBook::addRestingOrder(const Order& o) { addResting(o); }

void MapOrderBook::reset(std::string symbol) {
    bids_.clear();
    asks_.clear();
    orderIndex_.clear();
    symbol_ = std::move(symbol);
}

std::vector<Order> MapOrderBook::allOrders() const {
    std::vector<Order> out;
    for (const auto& [price, level] : bids_)
        for (const auto& o : level) out.push_back(o);
    for (const auto& [price, level] : asks_)
        for (const auto& o : level) out.push_back(o);
    return out;
}

const Order* MapOrderBook::getOrder(uint64_t orderId) const {
    auto it = orderIndex_.find(orderId);
    if (it == orderIndex_.end()) return nullptr;
    return &(*it->second.it);
}

size_t MapOrderBook::orderCount() const { return orderIndex_.size(); }

const std::string& MapOrderBook::symbol() const { return symbol_; }

std::vector<PriceLevel> MapOrderBook::bids(size_t maxLevels) const {
    std::vector<PriceLevel> out;
    for (const auto& [price, level] : bids_) {
        PriceLevel pl{price, 0, 0};
        for (const auto& o : level) {
            pl.totalQuantity += o.quantity;
            pl.orderCount++;
        }
        out.push_back(pl);
        if (maxLevels && out.size() >= maxLevels) break;
    }
    return out;
}

std::vector<PriceLevel> MapOrderBook::asks(size_t maxLevels) const {
    std::vector<PriceLevel> out;
    for (const auto& [price, level] : asks_) {
        PriceLevel pl{price, 0, 0};
        for (const auto& o : level) {
            pl.totalQuantity += o.quantity;
            pl.orderCount++;
        }
        out.push_back(pl);
        if (maxLevels && out.size() >= maxLevels) break;
    }
    return out;
}

std::optional<int64_t> MapOrderBook::bestBid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<int64_t> MapOrderBook::bestAsk() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

} // namespace app
