#include "ClusterHarness.h"

#include <iostream>
#include <cassert>
#include <random>
#include <algorithm>
#include <map>
#include <vector>

using namespace raft;
using namespace raft::test;

namespace {

// Election Safety: at most one leader per term (paper §3.4).
bool checkElectionSafety(ClusterHarness& cluster,
                          const std::vector<NodeId>& peers) {
    std::map<Term, int> leadersPerTerm;
    for (auto id : peers) {
        if (cluster.node(id).role() == Role::Leader) {
            Term t = cluster.node(id).term();
            leadersPerTerm[t]++;
        }
    }
    for (auto& [t, count] : leadersPerTerm) {
        if (count > 1) {
            std::cerr << "VIOLATION: " << count << " leaders in term " << t << "\n";
            return false;
        }
    }
    return true;
}

// Log Matching: if two nodes both have an entry at index i,
// they must agree on its term (paper §3.4, Log Matching Property).
bool checkLogMatching(ClusterHarness& cluster,
                       const std::vector<NodeId>& peers) {
    NodeId anyNode = peers[0];
    Index maxIdx = cluster.node(anyNode).lastIndex();

    for (Index i = 1; i <= maxIdx; ++i) {
        std::vector<std::pair<NodeId, Term>> terms;
        for (auto id : peers) {
            // Skip nodes that don't have this index.
            if (i > cluster.node(id).lastIndex()) continue;
            Term t = cluster.node(id).entryTerm(i);
            // term 0 means the entry doesn't exist — skip.
            if (t == 0) continue;
            terms.emplace_back(id, t);
        }
        for (size_t a = 0; a < terms.size(); ++a) {
            for (size_t b = a + 1; b < terms.size(); ++b) {
                if (terms[a].second != terms[b].second) {
                    std::cerr << "VIOLATION: node " << terms[a].first
                              << " has term " << terms[a].second
                              << " at index " << i
                              << ", but node " << terms[b].first
                              << " has term " << terms[b].second << "\n";
                    return false;
                }
            }
        }
    }
    return true;
}

} // namespace

int main() {
    std::vector<NodeId> peers = {1, 2, 3};
    std::mt19937 rng(42);  // fixed seed for reproducibility

    constexpr int numTrials = 200;
    constexpr int stepsPerTrial = 50;

    int violations = 0;

    for (int trial = 0; trial < numTrials; ++trial) {
        ClusterHarness cluster(peers);

        for (int step = 0; step < stepsPerTrial; ++step) {
            int action = rng() % 4;

            switch (action) {
                case 0: {
                    uint32_t ticks = 1 + rng() % 5;
                    cluster.step(ticks);
                    break;
                }
                case 1: {
                    NodeId target = 1 + rng() % 3;
                    std::vector<uint8_t> data = {static_cast<uint8_t>('a' + (rng() % 26))};
                    cluster.propose(target, data);
                    cluster.step(3);
                    break;
                }
                case 2: {
                    NodeId a = 1 + rng() % 3;
                    NodeId b = 1 + rng() % 3;
                    if (a != b) {
                        cluster.setDropFilter([a, b](NodeId from, NodeId to) {
                            return (from == a && to == b) || (from == b && to == a);
                        });
                    }
                    break;
                }
                case 3: {
                    cluster.clearDropFilter();
                    cluster.step(10);
                    break;
                }
            }

            if (!checkElectionSafety(cluster, peers)) {
                std::cerr << "Trial " << trial << " step " << step
                          << ": Election Safety violated\n";
                ++violations;
                break;
            }
        }

        cluster.clearDropFilter();
        cluster.step(1000);

        if (!checkLogMatching(cluster, peers)) {
            std::cerr << "Trial " << trial << ": Log Matching violated\n";
            ++violations;
        }
    }

    std::cout << "Ran " << numTrials << " random trials, "
              << violations << " violations\n";
    assert(violations == 0);
    std::cout << "test_property: PASS\n";
    return 0;
}
