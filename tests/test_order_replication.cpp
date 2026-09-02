#include "ClusterHarness.h"
#include "../app/statemachine/LOBStateMachine.h"
#include "../app/protocol/OrderProtocol.h"

#include <iostream>
#include <cassert>
#include <vector>
#include <memory>

using namespace raft;
using namespace raft::test;
using namespace app;
using namespace app::order_protocol;

namespace {

struct LobCluster {
    std::vector<NodeId> ids;
    ClusterHarness harness;
    std::map<NodeId, std::unique_ptr<LOBStateMachine>> fsms;

    explicit LobCluster(std::vector<NodeId> peers)
        : ids(peers), harness(peers) {
        for (auto id : peers) {
            fsms[id] = std::make_unique<LOBStateMachine>("LOB");
        }
        // Wire the apply callback: when step() processes committed entries,
        // apply them to the corresponding FSM immediately.
        harness.setApplyCallback([this](NodeId id, const std::vector<Entry>& entries) {
            for (const auto& e : entries) {
                fsms[id]->apply(e.data);
            }
        });
    }

    void step(uint32_t n = 1) { harness.step(n); }

    bool proposeOrder(const std::vector<uint8_t>& cmd) {
        NodeId lead = harness.leader();
        if (lead == 0) return false;
        return harness.propose(lead, cmd);
    }

    bool booksMatch() const {
        if (fsms.empty()) return true;
        auto refBids = fsms.begin()->second->bids();
        auto refAsks = fsms.begin()->second->asks();
        for (const auto& [id, fsm] : fsms) {
            if (fsm->bids() != refBids || fsm->asks() != refAsks) return false;
        }
        return true;
    }
};

void testBasicReplication() {
    LobCluster cluster({1, 2, 3});
    cluster.step(25);
    assert(cluster.harness.leader() != 0);
    std::cout << "Leader: node " << cluster.harness.leader() << "\n";

    assert(cluster.proposeOrder(encodeNew(1, Side::Buy, 10000, 10)));
    cluster.step(5);

    assert(cluster.booksMatch());
    for (const auto& [id, fsm] : cluster.fsms) {
        assert(fsm->orderCount() == 1);
        assert(fsm->bestBid().value() == 10000);
    }
    std::cout << "basic replication: all 3 nodes have bid=10000, 1 order\n";
}

void testMatchingReplication() {
    LobCluster cluster({1, 2, 3});
    cluster.step(25);
    assert(cluster.harness.leader() != 0);

    cluster.proposeOrder(encodeNew(1, Side::Sell, 10000, 10));
    cluster.step(5);
    assert(cluster.booksMatch());
    for (const auto& [id, fsm] : cluster.fsms) {
        assert(fsm->bestAsk().value() == 10000);
    }

    cluster.proposeOrder(encodeNew(2, Side::Buy, 10000, 4));
    cluster.step(5);
    assert(cluster.booksMatch());
    for (const auto& [id, fsm] : cluster.fsms) {
        assert(fsm->orderCount() == 1);
        assert(fsm->bestAsk().value() == 10000);
    }
    std::cout << "matching replication: all 3 nodes agree after match\n";
}

void testMultipleOrdersReplication() {
    LobCluster cluster({1, 2, 3});
    cluster.step(25);
    assert(cluster.harness.leader() != 0);

    for (int i = 1; i <= 10; ++i) {
        Side side = (i % 2 == 0) ? Side::Buy : Side::Sell;
        int64_t price = 10000 + (i % 3);
        cluster.proposeOrder(encodeNew(i, side, price, i * 10));
        cluster.step(3);
    }

    assert(cluster.booksMatch());
    for (const auto& [id, fsm] : cluster.fsms) {
        assert(fsm->orderCount() > 0);
    }
    std::cout << "10 orders replicated: all 3 nodes agree\n";
}

void testCancelReplication() {
    LobCluster cluster({1, 2, 3});
    cluster.step(25);
    assert(cluster.harness.leader() != 0);

    cluster.proposeOrder(encodeNew(1, Side::Buy, 10000, 10));
    cluster.step(5);
    assert(cluster.booksMatch());
    for (const auto& [id, fsm] : cluster.fsms) {
        assert(fsm->orderCount() == 1);
    }

    cluster.proposeOrder(encodeCancel(1));
    cluster.step(5);
    assert(cluster.booksMatch());
    for (const auto& [id, fsm] : cluster.fsms) {
        assert(fsm->orderCount() == 0);
    }
    std::cout << "cancel replicated: all 3 nodes have 0 orders\n";
}

void testPartitionHeal() {
    LobCluster cluster({1, 2, 3});
    cluster.step(25);
    NodeId lead = cluster.harness.leader();
    assert(lead != 0);

    // Partition: drop messages between two followers and the leader.
    NodeId follower1 = (lead == 1) ? 2 : 1;
    NodeId follower2 = (lead == 3) ? 2 : 3;
    cluster.harness.setDropFilter([lead, follower1, follower2](NodeId from, NodeId to) {
        return (from == lead && to == follower1) ||
               (from == follower1 && to == lead) ||
               (from == lead && to == follower2) ||
               (from == follower2 && to == lead);
    });

    cluster.proposeOrder(encodeNew(1, Side::Buy, 10000, 10));
    cluster.step(5);

    // Heal.
    cluster.harness.clearDropFilter();
    cluster.step(15);

    assert(cluster.booksMatch());
    for (const auto& [id, fsm] : cluster.fsms) {
        assert(fsm->orderCount() == 1);
    }
    std::cout << "partition heal: all 3 nodes converged\n";
}

void testConsistencyAfterChaos() {
    LobCluster cluster({1, 2, 3});
    cluster.step(25);
    assert(cluster.harness.leader() != 0);

    for (int i = 1; i <= 50; ++i) {
        Side side = (i % 2 == 0) ? Side::Buy : Side::Sell;
        int64_t price = 10000 + (i % 5) * 100;
        cluster.proposeOrder(encodeNew(i, side, price, 5));
        cluster.step(1 + (i % 3));
    }
    cluster.step(20);

    assert(cluster.booksMatch());
    std::cout << "chaos: 50 orders, all 3 nodes agree\n";
}

} // namespace

int main() {
    std::cout << "=== test_order_replication ===\n";
    testBasicReplication();
    testMatchingReplication();
    testMultipleOrdersReplication();
    testCancelReplication();
    testPartitionHeal();
    testConsistencyAfterChaos();
    std::cout << "test_order_replication: PASS\n";
    return 0;
}
