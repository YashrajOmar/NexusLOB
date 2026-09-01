#include "ClusterHarness.h"

#include <iostream>
#include <cassert>

using namespace raft;
using namespace raft::test;

int main() {
    // 3-node cluster, default election timeout (10 ticks).
    ClusterHarness cluster({1, 2, 3});

    // Step past the election timeout (15 ticks > randomized 10-20).
    // Some nodes may need a few more ticks to settle.
    cluster.step(25);

    // Verify exactly one leader exists.
    NodeId lead = cluster.leader();
    assert(lead != 0);
    std::cout << "Leader elected: node " << lead << "\n";

    // Verify the other two are followers.
    int leaderCount = 0;
    for (auto id : {1, 2, 3}) {
        if (cluster.node(id).role() == Role::Leader) {
            ++leaderCount;
        }
    }
    assert(leaderCount == 1);
    std::cout << "Exactly one leader (safety: Election Safety property)\n";

    // Verify the term advanced (election increments term).
    assert(cluster.node(lead).term() >= 1);
    std::cout << "Term: " << cluster.node(lead).term() << "\n";

    std::cout << "test_election: PASS\n";
    return 0;
}
