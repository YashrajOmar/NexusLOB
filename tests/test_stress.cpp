#include "ClusterHarness.h"

#include <iostream>
#include <cassert>
#include <string>
#include <vector>

using namespace raft;
using namespace raft::test;

int main() {
    std::vector<NodeId> peers = {1, 2, 3};
    ClusterHarness cluster(peers);

    // Elect a leader.
    cluster.step(25);
    NodeId lead = cluster.leader();
    assert(lead != 0);
    std::cout << "Leader: node " << lead << "\n";

    // Fire 10,000 proposals at the leader.
    constexpr int N = 10000;
    std::cout << "Proposing " << N << " entries...\n";
    for (int i = 0; i < N; ++i) {
        std::string cmd = "v" + std::to_string(i);
        bool ok = cluster.propose(lead,
            std::vector<uint8_t>(cmd.begin(), cmd.end()));
        assert(ok);
        // Step every 100 proposals to let replication progress.
        if (i % 100 == 0) {
            cluster.step(2);
        }
    }
    // Final drain.
    cluster.step(30);

    // Verify all 3 nodes have the same lastIndex.
    Index leaderLast = cluster.node(lead).lastIndex();
    std::cout << "Leader lastIndex: " << leaderLast << "\n";
    assert(leaderLast >= N);

    for (auto id : peers) {
        if (id == lead) continue;
        Index followerLast = cluster.node(id).lastIndex();
        assert(followerLast == leaderLast);
    }
    std::cout << "All nodes caught up to index " << leaderLast << "\n";

    // Verify entry terms match across all nodes (log consistency).
    for (Index i = 1; i <= leaderLast; ++i) {
        Term leaderTerm = cluster.node(lead).entryTerm(i);
        for (auto id : peers) {
            if (id == lead) continue;
            Term followerTerm = cluster.node(id).entryTerm(i);
            assert(followerTerm == leaderTerm);
        }
    }
    std::cout << "All " << leaderLast << " entries have consistent terms\n";

    std::cout << "test_stress: PASS\n";
    return 0;
}
