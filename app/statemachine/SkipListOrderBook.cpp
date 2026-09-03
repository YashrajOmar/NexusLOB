#include "statemachine/SkipListOrderBook.h"

#include <algorithm>

namespace app {

// ============================================================
// Construction
// ============================================================

SkipListOrderBook::SkipListOrderBook(std::string symbol)
    : symbol_(std::move(symbol)), bids_(65536, 0.5f), asks_(65536, 0.5f) {}

// ============================================================
// Mutations
// ============================================================

std::vector<Fill> SkipListOrderBook::newOrder(uint64_t orderId, Side side,
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

bool SkipListOrderBook::cancelOrder(uint64_t orderId) {
    auto it = orderIndex_.find(orderId);
    if (it == orderIndex_.end()) return false;

    Side    side   = it->second.side;
    int64_t price  = it->second.price;
    auto    listIt = it->second.it;

    if (side == Side::Buy) {
        auto node = bids_.find(price);
        if (node) {
            node->value.erase(listIt);
            if (node->value.empty()) bids_.remove(price);
        }
    } else {
        auto node = asks_.find(price);
        if (node) {
            node->value.erase(listIt);
            if (node->value.empty()) asks_.remove(price);
        }
    }
    orderIndex_.erase(it);
    return true;
}

bool SkipListOrderBook::modifyOrder(uint64_t orderId, int64_t newPrice,
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

// ============================================================
// Matching
// ============================================================

std::vector<Fill> SkipListOrderBook::match(Side takerSide, int64_t takerPrice,
                                            int64_t& remainingQty, uint64_t takerId) {
    std::vector<Fill> fills;

    while (remainingQty > 0) {
        std::shared_ptr<typename BidSkipList::Node> bestBidNode;
        std::shared_ptr<typename AskSkipList::Node> bestAskNode;
        LevelList* level = nullptr;
        int64_t    bestPrice = 0;

        if (takerSide == Side::Buy) {
            bestAskNode = asks_.best();
            if (!bestAskNode) break;
            bestPrice = bestAskNode->key;
            if (takerPrice < bestPrice) break;
            level = &bestAskNode->value;
        } else {
            bestBidNode = bids_.best();
            if (!bestBidNode) break;
            bestPrice = bestBidNode->key;
            if (takerPrice > bestPrice) break;
            level = &bestBidNode->value;
        }

        while (remainingQty > 0 && !level->empty()) {
            Order& maker = level->front();
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
                level->pop_front();
            }
        }
        if (level->empty()) {
            if (takerSide == Side::Buy) asks_.remove(bestPrice);
            else                        bids_.remove(bestPrice);
        }
    }
    return fills;
}

void SkipListOrderBook::addResting(const Order& o) {
    if (o.side == Side::Buy) {
        auto node = bids_.find(o.price);
        if (!node) {
            bids_.insert(o.price, LevelList{});
            node = bids_.find(o.price);
        }
        node->value.push_back(o);
        orderIndex_[o.id] = OrderLocation{o.side, o.price, std::prev(node->value.end())};
    } else {
        auto node = asks_.find(o.price);
        if (!node) {
            asks_.insert(o.price, LevelList{});
            node = asks_.find(o.price);
        }
        node->value.push_back(o);
        orderIndex_[o.id] = OrderLocation{o.side, o.price, std::prev(node->value.end())};
    }
}

void SkipListOrderBook::addRestingOrder(const Order& o) { addResting(o); }

// ============================================================
// Snapshot support
// ============================================================

std::vector<Order> SkipListOrderBook::allOrders() const {
    std::vector<Order> out;
    // Bids: best (highest) first
    auto n = bids_.best();
    while (n) {
        for (const auto& o : n->value) out.push_back(o);
        n = n->forward[0];
    }
    // Asks: best (lowest) first
    auto m = asks_.best();
    while (m) {
        for (const auto& o : m->value) out.push_back(o);
        m = m->forward[0];
    }
    return out;
}

void SkipListOrderBook::reset(std::string symbol) {
    symbol_ = std::move(symbol);
    orderIndex_.clear();
    // Rebuild empty skip lists by reassigning.
    bids_ = BidSkipList(65536, 0.5f);
    asks_ = AskSkipList(65536, 0.5f);
}

void SkipListOrderBook::rebuild(const std::vector<Order>& orders) {
    for (const auto& o : orders) addRestingOrder(o);
}

// ============================================================
// L3 queries
// ============================================================

const Order* SkipListOrderBook::getOrder(uint64_t orderId) const {
    auto it = orderIndex_.find(orderId);
    if (it == orderIndex_.end()) return nullptr;
    return &(*it->second.it);
}

size_t SkipListOrderBook::orderCount() const { return orderIndex_.size(); }

const std::string& SkipListOrderBook::symbol() const { return symbol_; }

// ============================================================
// L2 queries
// ============================================================

std::vector<PriceLevel> SkipListOrderBook::bids(size_t maxLevels) const {
    std::vector<PriceLevel> out;
    auto n = bids_.best();
    while (n) {
        PriceLevel pl{n->key, 0, 0};
        for (const auto& o : n->value) {
            pl.totalQuantity += o.quantity;
            pl.orderCount++;
        }
        out.push_back(pl);
        if (maxLevels && out.size() >= maxLevels) break;
        n = n->forward[0];
    }
    return out;
}

std::vector<PriceLevel> SkipListOrderBook::asks(size_t maxLevels) const {
    std::vector<PriceLevel> out;
    auto n = asks_.best();
    while (n) {
        PriceLevel pl{n->key, 0, 0};
        for (const auto& o : n->value) {
            pl.totalQuantity += o.quantity;
            pl.orderCount++;
        }
        out.push_back(pl);
        if (maxLevels && out.size() >= maxLevels) break;
        n = n->forward[0];
    }
    return out;
}

std::optional<int64_t> SkipListOrderBook::bestBid() const {
    auto n = bids_.best();
    if (!n) return std::nullopt;
    return n->key;
}

std::optional<int64_t> SkipListOrderBook::bestAsk() const {
    auto n = asks_.best();
    if (!n) return std::nullopt;
    return n->key;
}

} // namespace app
