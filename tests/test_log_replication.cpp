#include "ClusterHarness.h"

#include <iostream>
#include <cassert>
#include <string>

using namespace raft;
using namespace raft::test;

int main() {
    // 3-node cluster.
    ClusterHarness cluster({1, 2, 3});

    // Elect a leader.
    cluster.step(25);
    NodeId lead = cluster.leader();
    assert(lead != 0);
    std::cout << "Leader: node " << lead << "\n";

    // Propose 5 commands on the leader.
    for (int i = 0; i < 5; ++i) {
        std::string cmd = "SET key" + std::to_string(i) + " val" + std::to_string(i);
        bool ok = cluster.propose(lead,
            std::vector<uint8_t>(cmd.begin(), cmd.end()));
        assert(ok);
        // Step a few ticks to let the AppendEntries round-trip complete.
        cluster.step(3);
    }

    // After replication, all nodes should have the same lastIndex.
    Index leaderLast = cluster.node(lead).lastIndex();
    std::cout << "Leader lastIndex: " << leaderLast << "\n";
    assert(leaderLast >= 5);

    for (auto id : {1, 2, 3}) {
        if (id == lead) continue;
        Index followerLast = cluster.node(id).lastIndex();
        std::cout << "Follower " << id << " lastIndex: " << followerLast << "\n";
        assert(followerLast == leaderLast);
    }

    // Verify the entries match across all nodes (Log Matching Property).
    for (auto id : {1, 2, 3}) {
        for (Index i = 1; i <= leaderLast; ++i) {
            // Each entry should exist and have the same term across nodes.
            Term t = cluster.node(id).entryTerm(i);
            assert(t == cluster.node(lead).term());
        }
    }
    std::cout << "All " << leaderLast << " entries match across nodes\n";

    std::cout << "test_log_replication: PASS\n";
    return 0;
}
