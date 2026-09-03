#include "../app/statemachine/MapOrderBook.h"
#include "../app/statemachine/LOBStateMachine.h"
#include "../app/protocol/OrderProtocol.h"

#include <iostream>
#include <cassert>
#include <climits>

using namespace app;
using namespace app::order_protocol;

namespace {

void testEncodeDecodeNew() {
    auto cmd = encodeNew(42, Side::Buy, 10000, 5);
    assert(cmd.size() == 1 + 8 + 1 + 8 + 8);

    uint64_t orderId;
    Side     side;
    int64_t  price, quantity;
    assert(decodeNew(cmd, orderId, side, price, quantity));
    assert(orderId == 42);
    assert(side == Side::Buy);
    assert(price == 10000);
    assert(quantity == 5);
    std::cout << "encode/decode NEW: id=42 BUY @10000 qty=5\n";
}

void testEncodeDecodeCancel() {
    auto cmd = encodeCancel(99);
    assert(cmd.size() == 1 + 8);

    uint64_t orderId;
    assert(decodeCancel(cmd, orderId));
    assert(orderId == 99);
    std::cout << "encode/decode CXL: id=99\n";
}

void testEncodeDecodeModify() {
    auto cmd = encodeModify(7, 20000, 15);
    assert(cmd.size() == 1 + 8 + 8 + 8);

    uint64_t orderId;
    int64_t  newPrice, newQuantity;
    assert(decodeModify(cmd, orderId, newPrice, newQuantity));
    assert(orderId == 7);
    assert(newPrice == 20000);
    assert(newQuantity == 15);
    std::cout << "encode/decode MOD: id=7 → @20000 qty=15\n";
}

void testFsmNewNoMatch() {
    LOBStateMachine fsm(std::make_unique<MapOrderBook>("AAPL"));

    auto cmd = encodeNew(1, Side::Buy, 10000, 10);
    auto result = fsm.apply(cmd);

    std::vector<Fill> fills;
    bool rested;
    int64_t remaining;
    assert(decodeNewResult(result, fills, rested, remaining));
    assert(fills.empty());
    assert(rested);
    assert(remaining == 10);
    assert(fsm.orderCount() == 1);
    assert(fsm.bestBid().value() == 10000);
    std::cout << "FSM NEW (no match): rested, 10 @10000 bid\n";
}

void testFsmNewWithMatch() {
    LOBStateMachine fsm(std::make_unique<MapOrderBook>("AAPL"));

    // Resting ask: 5 @10000.
    fsm.apply(encodeNew(1, Side::Sell, 10000, 5));

    // Buy 12 @10000 → fills 5, 7 rests.
    auto result = fsm.apply(encodeNew(2, Side::Buy, 10000, 12));
    std::vector<Fill> fills;
    bool rested;
    int64_t remaining;
    assert(decodeNewResult(result, fills, rested, remaining));
    assert(fills.size() == 1);
    assert(fills[0].makerOrderId == 1);
    assert(fills[0].price == 10000);
    assert(fills[0].quantity == 5);
    assert(rested);
    assert(remaining == 7);
    assert(fsm.orderCount() == 1);  // only the resting 7
    assert(fsm.bestBid().value() == 10000);
    std::cout << "FSM NEW (match): filled 5 @10000, 7 rests on bid\n";
}

void testFsmNewFullFill() {
    LOBStateMachine fsm(std::make_unique<MapOrderBook>("AAPL"));

    fsm.apply(encodeNew(1, Side::Sell, 10000, 10));
    auto result = fsm.apply(encodeNew(2, Side::Buy, 10000, 10));
    std::vector<Fill> fills;
    bool rested;
    int64_t remaining;
    assert(decodeNewResult(result, fills, rested, remaining));
    assert(fills.size() == 1);
    assert(fills[0].quantity == 10);
    assert(!rested);
    assert(remaining == 0);
    assert(fsm.orderCount() == 0);
    std::cout << "FSM NEW (full fill): no rest, book empty\n";
}

void testFsmCancel() {
    LOBStateMachine fsm(std::make_unique<MapOrderBook>("AAPL"));
    fsm.apply(encodeNew(1, Side::Buy, 10000, 10));
    assert(fsm.orderCount() == 1);

    auto result = fsm.apply(encodeCancel(1));
    bool ok;
    assert(decodeSimpleResult(result, ok));
    assert(ok);
    assert(fsm.orderCount() == 0);

    // Cancel non-existent → ok=false.
    result = fsm.apply(encodeCancel(999));
    assert(decodeSimpleResult(result, ok));
    assert(!ok);
    std::cout << "FSM CXL: removed order 1, non-existent returns false\n";
}

void testFsmModify() {
    LOBStateMachine fsm(std::make_unique<MapOrderBook>("AAPL"));
    fsm.apply(encodeNew(1, Side::Sell, 10000, 10));
    fsm.apply(encodeNew(2, Side::Sell, 10000, 10));

    // Modify order 1 to 10100 (loses priority).
    auto result = fsm.apply(encodeModify(1, 10100, 10));
    bool ok;
    assert(decodeSimpleResult(result, ok));
    assert(ok);
    assert(fsm.bestAsk().value() == 10000);  // order 2 still at 10000

    // New buy @10000 should hit order 2 (priority preserved).
    auto newResult = fsm.apply(encodeNew(3, Side::Buy, 10000, 10));
    std::vector<Fill> fills;
    bool rested;
    int64_t remaining;
    assert(decodeNewResult(newResult, fills, rested, remaining));
    assert(fills.size() == 1);
    assert(fills[0].makerOrderId == 2);
    std::cout << "FSM MOD: order1 10000→10100, order2 matched first\n";
}

void testFsmRejectDuplicate() {
    LOBStateMachine fsm(std::make_unique<MapOrderBook>("AAPL"));
    fsm.apply(encodeNew(1, Side::Buy, 10000, 10));

    auto result = fsm.apply(encodeNew(1, Side::Buy, 10000, 5));
    bool ok;
    assert(decodeSimpleResult(result, ok));
    assert(!ok);
    assert(fsm.orderCount() == 1);
    std::cout << "FSM reject duplicate orderId\n";
}

void testFsmSnapshotRestore() {
    LOBStateMachine fsm(std::make_unique<MapOrderBook>("AAPL"));
    fsm.apply(encodeNew(1, Side::Buy,  10000, 10));
    fsm.apply(encodeNew(2, Side::Buy,  10000, 5));   // FIFO: 1 then 2
    fsm.apply(encodeNew(3, Side::Sell, 10100, 8));

    auto snap = fsm.snapshot();
    assert(!snap.empty());

    LOBStateMachine restored(std::make_unique<MapOrderBook>("TEMP"));
    restored.restore(snap);

    assert(restored.symbol() == "AAPL");
    assert(restored.orderCount() == 3);
    assert(restored.bestBid().value() == 10000);
    assert(restored.bestAsk().value() == 10100);

    // Verify FIFO priority survived the snapshot: a sell @10000 qty 12
    // should hit order 1 (10) before order 2 (2 of 5).
    auto result = restored.apply(encodeNew(99, Side::Sell, 10000, 12));
    std::vector<Fill> fills;
    bool rested;
    int64_t remaining;
    assert(decodeNewResult(result, fills, rested, remaining));
    assert(fills.size() == 2);
    assert(fills[0].makerOrderId == 1);  // FIFO: order 1 first
    assert(fills[0].quantity == 10);
    assert(fills[1].makerOrderId == 2);
    assert(fills[1].quantity == 2);
    assert(!rested);
    assert(remaining == 0);
    std::cout << "snapshot/restore: FIFO priority preserved (order1 before order2)\n";
}

void testFsmDeterministicAcrossInstances() {
    // Two independent FSMs fed the same commands must end up in the same state.
    LOBStateMachine a(std::make_unique<MapOrderBook>("AAPL"));
    LOBStateMachine b(std::make_unique<MapOrderBook>("AAPL"));

    std::vector<std::vector<uint8_t>> cmds = {
        encodeNew(1,  Side::Buy,  9900, 10),
        encodeNew(2,  Side::Buy,  10000, 5),
        encodeNew(3,  Side::Sell, 10100, 8),
        encodeNew(4,  Side::Buy,  10000, 3),   // matches nothing, rests
        encodeNew(5,  Side::Sell, 10000, 6),   // matches order 2 (5) + order 4 (1)
        encodeCancel(1),
        encodeModify(4, 9900, 0),             // modify to qty=0 = cancel
    };

    for (const auto& c : cmds) {
        auto ra = a.apply(c);
        auto rb = b.apply(c);
        assert(ra == rb);  // identical results
    }

    auto sa = a.snapshot();
    auto sb = b.snapshot();
    assert(sa == sb);  // identical snapshots

    assert(a.orderCount() == b.orderCount());
    assert(a.bestBid() == b.bestBid());
    assert(a.bestAsk() == b.bestAsk());
    std::cout << "determinism: two FSMs fed same commands → identical state\n";
}

void testFsmL2Queries() {
    LOBStateMachine fsm(std::make_unique<MapOrderBook>("AAPL"));
    fsm.apply(encodeNew(1, Side::Buy,  10000, 5));
    fsm.apply(encodeNew(2, Side::Buy,  10000, 5));
    fsm.apply(encodeNew(3, Side::Buy,  9900, 3));
    fsm.apply(encodeNew(4, Side::Sell, 10100, 4));
    fsm.apply(encodeNew(5, Side::Sell, 10200, 6));

    auto b = fsm.bids();
    assert(b.size() == 2);
    assert(b[0].price == 10000 && b[0].totalQuantity == 10 && b[0].orderCount == 2);
    assert(b[1].price == 9900 && b[1].totalQuantity == 3);

    auto a = fsm.asks();
    assert(a.size() == 2);
    assert(a[0].price == 10100 && a[0].totalQuantity == 4);
    assert(a[1].price == 10200 && a[1].totalQuantity == 6);

    assert(fsm.bids(1).size() == 1);
    assert(fsm.asks(1).size() == 1);
    std::cout << "FSM L2 queries: bids [10000×10, 9900×3], asks [10100×4, 10200×6]\n";
}

void testNewResultRoundTrip() {
    std::vector<Fill> fills = {
        {10, 20, 10000, 5, Side::Buy},
        {11, 20, 10001, 3, Side::Buy},
    };
    auto encoded = encodeNewResult(fills, true, 7);
    std::vector<Fill> decoded;
    bool rested;
    int64_t remaining;
    assert(decodeNewResult(encoded, decoded, rested, remaining));
    assert(decoded.size() == 2);
    assert(decoded[0].makerOrderId == 10 && decoded[0].quantity == 5);
    assert(decoded[1].makerOrderId == 11 && decoded[1].quantity == 3);
    assert(rested);
    assert(remaining == 7);
    std::cout << "NEW result round-trip: 2 fills, rested, remaining=7\n";
}

void testEmptyAndMalformed() {
    LOBStateMachine fsm(std::make_unique<MapOrderBook>("AAPL"));

    // Empty command.
    auto r = fsm.apply({});
    bool ok;
    assert(decodeSimpleResult(r, ok));
    assert(!ok);

    // Unknown opcode.
    r = fsm.apply({0xFF});
    assert(decodeSimpleResult(r, ok));
    assert(!ok);
    std::cout << "empty/malformed commands rejected\n";
}

} // namespace

int main() {
    std::cout << "=== test_order_protocol ===\n";
    testEncodeDecodeNew();
    testEncodeDecodeCancel();
    testEncodeDecodeModify();
    testNewResultRoundTrip();
    testFsmNewNoMatch();
    testFsmNewWithMatch();
    testFsmNewFullFill();
    testFsmCancel();
    testFsmModify();
    testFsmRejectDuplicate();
    testFsmSnapshotRestore();
    testFsmDeterministicAcrossInstances();
    testFsmL2Queries();
    testEmptyAndMalformed();
    std::cout << "test_order_protocol: PASS\n";
    return 0;
}
