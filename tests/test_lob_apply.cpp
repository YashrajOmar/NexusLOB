#include "../app/statemachine/OrderBook.h"

#include <iostream>
#include <cassert>
#include <climits>
#include <chrono>
#include <numeric>

using namespace app;

namespace {

int64_t totalFillQty(const std::vector<Fill>& fills) {
    int64_t sum = 0;
    for (const auto& f : fills) sum += f.quantity;
    return sum;
}

void testEmptyBook() {
    OrderBook book("AAPL");
    assert(!book.bestBid().has_value());
    assert(!book.bestAsk().has_value());
    assert(book.orderCount() == 0);
    assert(book.bids().empty());
    assert(book.asks().empty());
    assert(book.getOrder(1) == nullptr);
    std::cout << "empty book: BBO=n/a, 0 orders\n";
}

void testNoCrossRestsOnBook() {
    OrderBook book("AAPL");

    // Buy at 100, no asks to cross → rests on bid side.
    auto fills = book.newOrder(1, Side::Buy, 100, 10, 1);
    assert(fills.empty());
    assert(book.bestBid().value() == 100);
    assert(!book.bestAsk().has_value());
    assert(book.orderCount() == 1);

    // Sell at 105, doesn't cross 100 → rests on ask side.
    fills = book.newOrder(2, Side::Sell, 105, 5, 2);
    assert(fills.empty());
    assert(book.bestBid().value() == 100);
    assert(book.bestAsk().value() == 105);
    assert(book.orderCount() == 2);
    std::cout << "no-cross: bid=100 (10), ask=105 (5)\n";
}

void testBasicMatch() {
    OrderBook book("AAPL");

    // Resting sell at 100, qty 10.
    book.newOrder(1, Side::Sell, 100, 10, 1);
    assert(book.bestAsk().value() == 100);

    // Buy at 100, qty 4 → crosses, fills 4, maker has 6 left.
    auto fills = book.newOrder(2, Side::Buy, 100, 4, 2);
    assert(fills.size() == 1);
    assert(fills[0].makerOrderId == 1);
    assert(fills[0].takerOrderId == 2);
    assert(fills[0].price == 100);
    assert(fills[0].quantity == 4);
    assert(fills[0].takerSide == Side::Buy);
    assert(totalFillQty(fills) == 4);

    // Taker fully filled, doesn't rest.
    assert(book.orderCount() == 1);
    assert(book.bestAsk().value() == 100);

    const Order* maker = book.getOrder(1);
    assert(maker != nullptr);
    assert(maker->quantity == 6);
    std::cout << "basic match: bought 4 @100, maker has 6 left\n";
}

void testPartialFillRests() {
    OrderBook book("AAPL");

    // Sell at 100, qty 5.
    book.newOrder(1, Side::Sell, 100, 5, 1);

    // Buy at 100, qty 12 → fills 5, 7 rests on bid side.
    auto fills = book.newOrder(2, Side::Buy, 100, 12, 2);
    assert(fills.size() == 1);
    assert(fills[0].quantity == 5);
    assert(totalFillQty(fills) == 5);

    // Maker gone (fully filled), taker rests with 7.
    assert(book.orderCount() == 1);
    assert(!book.bestAsk().has_value());
    assert(book.bestBid().value() == 100);
    const Order* taker = book.getOrder(2);
    assert(taker != nullptr);
    assert(taker->quantity == 7);
    std::cout << "partial fill: bought 5 @100, 7 rests on bid\n";
}

void testPriceTimePriority() {
    OrderBook book("AAPL");

    // Two sells at 100, seq 1 then seq 2. FIFO: seq 1 matched first.
    book.newOrder(1, Side::Sell, 100, 10, 1);
    book.newOrder(2, Side::Sell, 100, 10, 2);

    // Buy 15 @100 → should fill 10 from order 1, then 5 from order 2.
    auto fills = book.newOrder(3, Side::Buy, 100, 15, 3);
    assert(fills.size() == 2);
    assert(fills[0].makerOrderId == 1);
    assert(fills[0].quantity == 10);
    assert(fills[1].makerOrderId == 2);
    assert(fills[1].quantity == 5);
    assert(totalFillQty(fills) == 15);

    // Order 2 has 5 left on the ask.
    assert(book.bestAsk().value() == 100);
    const Order* m2 = book.getOrder(2);
    assert(m2 != nullptr);
    assert(m2->quantity == 5);
    std::cout << "price-time: FIFO at 100 → order1(10) then order2(5/10 left)\n";
}

void testPricePriority() {
    OrderBook book("AAPL");

    // Sells at 101 and 100. Better-priced (100) matched first.
    book.newOrder(1, Side::Sell, 101, 10, 1);
    book.newOrder(2, Side::Sell, 100, 10, 2);
    assert(book.bestAsk().value() == 100);

    // Buy 10 @101 → crosses both, takes the 100 level first.
    auto fills = book.newOrder(3, Side::Buy, 101, 25, 3);
    assert(fills.size() == 2);
    assert(fills[0].makerOrderId == 2);  // 100 level first
    assert(fills[0].price == 100);
    assert(fills[0].quantity == 10);
    assert(fills[1].makerOrderId == 1);  // then 101 level
    assert(fills[1].price == 101);
    assert(fills[1].quantity == 10);
    assert(totalFillQty(fills) == 20);

    // 5 rests on bid at 101.
    assert(book.bestBid().value() == 101);
    std::cout << "price priority: swept 100 then 101, 5 rests @101\n";
}

void testSweepMultipleLevels() {
    OrderBook book("AAPL");

    // Three ask levels: 100, 101, 102.
    book.newOrder(1, Side::Sell, 100, 5, 1);
    book.newOrder(2, Side::Sell, 101, 5, 2);
    book.newOrder(3, Side::Sell, 102, 5, 3);

    // Buy market (INT64_MAX), qty 8 → sweeps 100 (5) then part of 101 (3).
    auto fills = book.newOrder(4, Side::Buy, INT64_MAX, 8, 4);
    assert(fills.size() == 2);
    assert(fills[0].price == 100 && fills[0].quantity == 5);
    assert(fills[1].price == 101 && fills[1].quantity == 3);
    assert(totalFillQty(fills) == 8);
    assert(book.bestAsk().value() == 101);
    assert(book.getOrder(2)->quantity == 2);
    std::cout << "sweep: market buy 8 → 5@100 + 3@101\n";
}

void testSellSideMatching() {
    OrderBook book("AAPL");

    // Resting bids at 99 (10) and 98 (10).
    book.newOrder(1, Side::Buy, 99, 10, 1);
    book.newOrder(2, Side::Buy, 98, 10, 2);

    // Sell at 98 (market-ish) qty 15 → fills 10@99, then 5@98.
    auto fills = book.newOrder(3, Side::Sell, 98, 15, 3);
    assert(fills.size() == 2);
    assert(fills[0].makerOrderId == 1);
    assert(fills[0].price == 99);
    assert(fills[0].quantity == 10);
    assert(fills[1].makerOrderId == 2);
    assert(fills[1].price == 98);
    assert(fills[1].quantity == 5);
    assert(totalFillQty(fills) == 15);
    assert(book.bestBid().value() == 98);
    assert(book.getOrder(2)->quantity == 5);
    std::cout << "sell-side: sell 15 → 10@99 + 5@98\n";
}

void testCancelOrder() {
    OrderBook book("AAPL");
    book.newOrder(1, Side::Buy, 100, 10, 1);
    book.newOrder(2, Side::Buy, 99, 10, 2);
    assert(book.orderCount() == 2);

    assert(book.cancelOrder(1));
    assert(book.orderCount() == 1);
    assert(book.bestBid().value() == 99);
    assert(book.getOrder(1) == nullptr);

    // Cancelling a non-existent order returns false.
    assert(!book.cancelOrder(999));
    std::cout << "cancel: removed order1, best bid now 99\n";
}

void testModifyOrder() {
    OrderBook book("AAPL");
    book.newOrder(1, Side::Sell, 100, 10, 1);
    book.newOrder(2, Side::Sell, 100, 10, 2);

    // Modify order 1 to price 101 (cancel-replace → loses priority).
    assert(book.modifyOrder(1, 101, 10, 3));
    assert(book.bestAsk().value() == 100);  // order 2 now at front of 100
    const Order* m1 = book.getOrder(1);
    assert(m1 != nullptr);
    assert(m1->price == 101);

    // A new buy @100 should hit order 2 first (order 1 moved to 101).
    auto fills = book.newOrder(3, Side::Buy, 100, 10, 4);
    assert(fills.size() == 1);
    assert(fills[0].makerOrderId == 2);
    std::cout << "modify: order1 moved 100→101, order2 matched first\n";
}

void testL2Depth() {
    OrderBook book("AAPL");
    // Bids: 100×(5+5), 99×3
    book.newOrder(1, Side::Buy, 100, 5, 1);
    book.newOrder(2, Side::Buy, 100, 5, 2);
    book.newOrder(3, Side::Buy, 99, 3, 3);
    // Asks: 101×4, 102×6
    book.newOrder(4, Side::Sell, 101, 4, 4);
    book.newOrder(5, Side::Sell, 102, 6, 5);

    auto b = book.bids();
    assert(b.size() == 2);
    assert(b[0].price == 100 && b[0].totalQuantity == 10 && b[0].orderCount == 2);
    assert(b[1].price == 99 && b[1].totalQuantity == 3 && b[1].orderCount == 1);

    auto a = book.asks();
    assert(a.size() == 2);
    assert(a[0].price == 101 && a[0].totalQuantity == 4 && a[0].orderCount == 1);
    assert(a[1].price == 102 && a[1].totalQuantity == 6 && a[1].orderCount == 1);

    // maxLevels truncation.
    assert(book.bids(1).size() == 1);
    assert(book.asks(1).size() == 1);
    std::cout << "L2 depth: bids [100×10, 99×3], asks [101×4, 102×6]\n";
}

void testDuplicateIdRejected() {
    OrderBook book("AAPL");
    auto fills = book.newOrder(1, Side::Buy, 100, 10, 1);
    assert(fills.empty());
    // Same id → rejected, returns empty, no second order.
    fills = book.newOrder(1, Side::Buy, 100, 10, 2);
    assert(fills.empty());
    assert(book.orderCount() == 1);
    std::cout << "duplicate id rejected\n";
}

void testZeroQuantityRejected() {
    OrderBook book("AAPL");
    auto fills = book.newOrder(1, Side::Buy, 100, 0, 1);
    assert(fills.empty());
    assert(book.orderCount() == 0);
    std::cout << "zero-quantity rejected\n";
}

void testLatencyBaseline() {
    // "Measure first": time in-memory apply() equivalent (newOrder).
    OrderBook book("BENCH");
    // Pre-populate a deep book.
    for (uint64_t i = 1; i <= 1000; ++i)
        book.newOrder(i, Side::Sell, static_cast<int64_t>(10000 + i), 100, i);
    for (uint64_t i = 1001; i <= 2000; ++i)
        book.newOrder(i, Side::Buy, static_cast<int64_t>(10000 - (i - 1000)), 100, i);

    constexpr int N = 100000;
    std::chrono::nanoseconds total{0};
    int64_t filledTotal = 0;
    for (int i = 0; i < N; ++i) {
        uint64_t id = 100000 + i;
        int64_t price = 10050;
        int64_t qty = 1;
        auto t0 = std::chrono::high_resolution_clock::now();
        auto fills = book.newOrder(id, (i % 2 == 0) ? Side::Buy : Side::Sell,
                                   price, qty, id);
        auto t1 = std::chrono::high_resolution_clock::now();
        total += t1 - t0;
        filledTotal += totalFillQty(fills);
    }

    double avgNs = static_cast<double>(total.count()) / N;
    std::cout << "latency baseline (newOrder, " << N << " ops): "
              << "avg=" << avgNs << " ns, "
              << "filled=" << filledTotal << "\n";
    assert(avgNs > 0);
}

} // namespace

int main() {
    std::cout << "=== test_lob_apply ===\n";
    testEmptyBook();
    testNoCrossRestsOnBook();
    testBasicMatch();
    testPartialFillRests();
    testPriceTimePriority();
    testPricePriority();
    testSweepMultipleLevels();
    testSellSideMatching();
    testCancelOrder();
    testModifyOrder();
    testL2Depth();
    testDuplicateIdRejected();
    testZeroQuantityRejected();
    testLatencyBaseline();
    std::cout << "test_lob_apply: PASS\n";
    return 0;
}
