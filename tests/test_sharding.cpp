#include "ClusterHarness.h"
#include "../app/statemachine/MapOrderBook.h"
#include "../app/statemachine/LOBStateMachine.h"
#include "../app/protocol/OrderProtocol.h"

#include <iostream>
#include <cassert>
#include <vector>
#include <memory>
#include <map>

using namespace raft;
using namespace raft::test;
using namespace app;
using namespace app::order_protocol;

namespace {

// A sharded cluster: N nodes, each with multiple Raft groups (one per symbol).
// Each symbol gets its own ClusterHarness + FSMs. Orders are routed to the
// correct harness based on the symbol.
struct ShardedCluster {
    std::vector<NodeId> ids;
    std::vector<std::string> symbols;
    std::map<std::string, std::unique_ptr<ClusterHarness>> harnesses;
    std::map<std::string, std::map<NodeId, std::unique_ptr<LOBStateMachine>>> fsms;

    ShardedCluster(std::vector<NodeId> peers, std::vector<std::string> syms)
        : ids(peers), symbols(syms) {
        for (const auto& sym : symbols) {
            harnesses[sym] = std::make_unique<ClusterHarness>(peers);
            for (auto id : peers) {
                fsms[sym][id] = std::make_unique<LOBStateMachine>(
                    std::make_unique<MapOrderBook>(sym));
            }
            // Wire apply callback for this symbol.
            harnesses[sym]->setApplyCallback(
                [this, sym](NodeId id, const std::vector<Entry>& entries) {
                    for (const auto& e : entries) {
                        fsms[sym][id]->apply(e.data);
                    }
                });
        }
    }

    void step(uint32_t n = 1) {
        for (auto& [sym, h] : harnesses) h->step(n);
    }

    bool propose(const std::string& sym, const std::vector<uint8_t>& cmd) {
        auto& h = *harnesses[sym];
        NodeId lead = h.leader();
        if (lead == 0) return false;
        return h.propose(lead, cmd);
    }

    NodeId leader(const std::string& sym) const {
        return harnesses.at(sym)->leader();
    }

    // Check all nodes agree on a symbol's book.
    bool booksMatch(const std::string& sym) const {
        auto& f = fsms.at(sym);
        auto refBids = f.begin()->second->bids();
        auto refAsks = f.begin()->second->asks();
        for (const auto& [id, fsm] : f) {
            if (fsm->bids() != refBids || fsm->asks() != refAsks) return false;
        }
        return true;
    }
};

void testMultiSymbolIndependent() {
    ShardedCluster cluster({1, 2, 3}, {"AAPL", "GOOG", "MSFT"});
    cluster.step(25);

    // Elect leaders for each symbol.
    for (const auto& sym : cluster.symbols) {
        assert(cluster.leader(sym) != 0);
        std::cout << sym << " leader: node " << cluster.leader(sym) << "\n";
    }

    // Submit orders to AAPL.
    cluster.propose("AAPL", encodeNew(1, Side::Buy, 10000, 10));
    cluster.step(5);
    assert(cluster.booksMatch("AAPL"));
    for (const auto& [id, fsm] : cluster.fsms["AAPL"]) {
        assert(fsm->orderCount() == 1);
        assert(fsm->bestBid().value() == 10000);
    }

    // GOOG should be empty.
    for (const auto& [id, fsm] : cluster.fsms["GOOG"]) {
        assert(fsm->orderCount() == 0);
    }

    // Submit orders to GOOG.
    cluster.propose("GOOG", encodeNew(1, Side::Sell, 20000, 5));
    cluster.step(5);
    assert(cluster.booksMatch("GOOG"));
    for (const auto& [id, fsm] : cluster.fsms["GOOG"]) {
        assert(fsm->orderCount() == 1);
        assert(fsm->bestAsk().value() == 20000);
    }

    // AAPL still has only 1 order.
    for (const auto& [id, fsm] : cluster.fsms["AAPL"]) {
        assert(fsm->orderCount() == 1);
    }

    std::cout << "multi-symbol independent: AAPL and GOOG isolated\n";
}

void testMultiSymbolMatching() {
    ShardedCluster cluster({1, 2, 3}, {"AAPL", "GOOG"});
    cluster.step(25);

    // AAPL: resting sell, then buy that crosses.
    cluster.propose("AAPL", encodeNew(1, Side::Sell, 10000, 10));
    cluster.step(5);
    cluster.propose("AAPL", encodeNew(2, Side::Buy, 10000, 4));
    cluster.step(5);

    assert(cluster.booksMatch("AAPL"));
    for (const auto& [id, fsm] : cluster.fsms["AAPL"]) {
        assert(fsm->orderCount() == 1);
        assert(fsm->bestAsk().value() == 10000);
    }

    // GOOG: different price, different orders.
    cluster.propose("GOOG", encodeNew(1, Side::Buy, 50000, 20));
    cluster.step(5);
    cluster.propose("GOOG", encodeNew(2, Side::Sell, 51000, 10));
    cluster.step(5);

    assert(cluster.booksMatch("GOOG"));
    for (const auto& [id, fsm] : cluster.fsms["GOOG"]) {
        assert(fsm->orderCount() == 2);
        assert(fsm->bestBid().value() == 50000);
        assert(fsm->bestAsk().value() == 51000);
    }

    std::cout << "multi-symbol matching: each book matches independently\n";
}

void testSymbolIsolation() {
    ShardedCluster cluster({1, 2, 3}, {"AAPL", "GOOG"});
    cluster.step(25);

    // Submit 10 orders to AAPL.
    for (int i = 1; i <= 10; ++i) {
        cluster.propose("AAPL", encodeNew(i, Side::Buy, 10000 + i, i * 10));
        cluster.step(3);
    }

    // Submit 10 orders to GOOG.
    for (int i = 1; i <= 10; ++i) {
        cluster.propose("GOOG", encodeNew(i, Side::Sell, 50000 + i, i * 5));
        cluster.step(3);
    }

    cluster.step(20);

    // AAPL has 10 orders, GOOG has 10 orders, no interference.
    assert(cluster.booksMatch("AAPL"));
    assert(cluster.booksMatch("GOOG"));
    for (const auto& [id, fsm] : cluster.fsms["AAPL"]) {
        assert(fsm->orderCount() == 10);
    }
    for (const auto& [id, fsm] : cluster.fsms["GOOG"]) {
        assert(fsm->orderCount() == 10);
    }

    // Cancel an AAPL order — GOOG unaffected.
    cluster.propose("AAPL", encodeCancel(1));
    cluster.step(5);
    assert(cluster.booksMatch("AAPL"));
    assert(cluster.booksMatch("GOOG"));
    for (const auto& [id, fsm] : cluster.fsms["AAPL"]) {
        assert(fsm->orderCount() == 9);
    }
    for (const auto& [id, fsm] : cluster.fsms["GOOG"]) {
        assert(fsm->orderCount() == 10);
    }

    std::cout << "symbol isolation: cancel AAPL does not affect GOOG\n";
}

void testMultiSymbolPartition() {
    ShardedCluster cluster({1, 2, 3}, {"AAPL", "GOOG"});
    cluster.step(25);

    // Partition AAPL's leader from one follower.
    NodeId aaplLead = cluster.leader("AAPL");
    cluster.harnesses["AAPL"]->setDropFilter(
        [aaplLead](NodeId from, NodeId to) {
            return (from == aaplLead && to != aaplLead) ||
                   (to == aaplLead && from != aaplLead);
        });

    // Submit orders during partition.
    cluster.propose("AAPL", encodeNew(1, Side::Buy, 10000, 10));
    cluster.propose("GOOG", encodeNew(1, Side::Buy, 50000, 10));
    cluster.step(5);

    // Heal AAPL.
    cluster.harnesses["AAPL"]->clearDropFilter();
    cluster.step(15);

    // Both symbols converge.
    assert(cluster.booksMatch("AAPL"));
    assert(cluster.booksMatch("GOOG"));
    for (const auto& [id, fsm] : cluster.fsms["AAPL"]) {
        assert(fsm->orderCount() == 1);
    }
    for (const auto& [id, fsm] : cluster.fsms["GOOG"]) {
        assert(fsm->orderCount() == 1);
    }

    std::cout << "multi-symbol partition: AAPL partitioned, GOOG unaffected, both converge\n";
}

void testShardedProtocolRoundTrip() {
    // Test the sharded protocol encode/decode.
    auto cmd = encodeShardedNew("AAPL", 42, Side::Buy, 10000, 5);
    std::string sym;
    uint64_t orderId;
    Side side;
    int64_t price, qty;
    assert(decodeShardedNew(cmd, sym, orderId, side, price, qty));
    assert(sym == "AAPL");
    assert(orderId == 42);
    assert(side == Side::Buy);
    assert(price == 10000);
    assert(qty == 5);

    auto cmd2 = encodeShardedCancel("GOOG", 99);
    std::string sym2;
    uint64_t orderId2;
    assert(decodeShardedCancel(cmd2, sym2, orderId2));
    assert(sym2 == "GOOG");
    assert(orderId2 == 99);

    auto cmd3 = encodeShardedModify("MSFT", 7, 20000, 15);
    std::string sym3;
    uint64_t orderId3;
    int64_t np, nq;
    assert(decodeShardedModify(cmd3, sym3, orderId3, np, nq));
    assert(sym3 == "MSFT");
    assert(orderId3 == 7);
    assert(np == 20000);
    assert(nq == 15);

    std::cout << "sharded protocol round-trip: AAPL, GOOG, MSFT\n";
}

} // namespace

int main() {
    std::cout << "=== test_sharding ===\n";
    testShardedProtocolRoundTrip();
    testMultiSymbolIndependent();
    testMultiSymbolMatching();
    testSymbolIsolation();
    testMultiSymbolPartition();
    std::cout << "test_sharding: PASS\n";
    return 0;
}
