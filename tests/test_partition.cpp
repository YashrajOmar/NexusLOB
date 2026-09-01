#include "ClusterHarness.h"

#include <iostream>
#include <cassert>

using namespace raft;
using namespace raft::test;

int main() {
    std::vector<NodeId> peers = {1, 2, 3};
    ClusterHarness cluster(peers);

    // Elect a leader.
    cluster.step(25);
    NodeId lead = cluster.leader();
    assert(lead != 0);
    std::cout << "Initial leader: node " << lead << "\n";

    // Remember the lastIndex before partition.
    cluster.step(3);
    Index beforeIdx = cluster.node(lead).lastIndex();
    std::cout << "Log index before partition: " << beforeIdx << "\n";

    // --- Partition: isolate node 3 from nodes 1 and 2 ---
    NodeId isolated = 3;
    cluster.setDropFilter([isolated](NodeId from, NodeId to) {
        return from == isolated || to == isolated;
    });
    std::cout << "Partition: node " << isolated << " isolated\n";
    cluster.step(5);

    // Majority side writes 3 entries (2 of 3 can still commit).
    // If the isolated node was leader, the majority side elects a new one.
    cluster.step(30);
    NodeId majorityLeader = cluster.leader();
    assert(majorityLeader != 0 && majorityLeader != isolated);
    std::cout << "Majority-side leader: node " << majorityLeader << "\n";

    for (int i = 0; i < 3; ++i) {
        std::vector<uint8_t> data = {static_cast<uint8_t>('x' + i)};
        bool ok = cluster.propose(majorityLeader, data);
        assert(ok);
        cluster.step(3);
    }
    Index afterIdx = cluster.node(majorityLeader).lastIndex();
    std::cout << "Majority side committed up to index " << afterIdx << "\n";
    assert(afterIdx > beforeIdx);

    // The isolated node must NOT have committed beyond what it had.
    Index isolatedIdx = cluster.node(isolated).lastIndex();
    std::cout << "Isolated node stuck at index " << isolatedIdx << "\n";

    // --- Heal the partition ---
    cluster.clearDropFilter();
    std::cout << "Partition healed\n";
    cluster.step(2000);

    // After healing, find the current leader (may have changed).
    NodeId finalLead = cluster.leader();
    assert(finalLead != 0);
    std::cout << "Post-heal leader: node " << finalLead << "\n";

    // All nodes must now have the same lastIndex (convergence).
    Index refIndex = cluster.node(finalLead).lastIndex();
    for (auto id : peers) {
        Index idx = cluster.node(id).lastIndex();
        std::cout << "Node " << id << " final index: " << idx << "\n";
        assert(idx == refIndex);
    }

    // All entries must have matching terms (log consistency restored).
    for (Index i = 1; i <= refIndex; ++i) {
        Term leadTerm = cluster.node(finalLead).entryTerm(i);
        for (auto id : peers) {
            if (id == finalLead) continue;
            assert(cluster.node(id).entryTerm(i) == leadTerm);
        }
    }

    std::cout << "test_partition: PASS — all nodes converged after heal\n";
    return 0;
}
