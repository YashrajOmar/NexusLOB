#include "ClusterHarness.h"
#include "../app/statemachine/LOBStateMachine.h"
#include "../app/protocol/OrderProtocol.h"

#include <iostream>
#include <cassert>
#include <vector>
#include <memory>
#include <chrono>
#include <algorithm>
#include <cmath>

using namespace raft;
using namespace raft::test;
using namespace app;
using namespace app::order_protocol;
using Clock = std::chrono::high_resolution_clock;

int main() {
    std::vector<NodeId> peers = {1, 2, 3};
    ClusterHarness harness(peers);

    std::map<NodeId, std::unique_ptr<LOBStateMachine>> fsms;
    std::map<NodeId, size_t> applyCount;
    for (auto id : peers) {
        fsms[id] = std::make_unique<LOBStateMachine>("LOB");
        applyCount[id] = 0;
    }
    harness.setApplyCallback([&](NodeId id, const std::vector<Entry>& entries) {
        for (const auto& e : entries) {
            fsms[id]->apply(e.data);
            applyCount[id]++;
        }
    });

    // Elect a leader.
    harness.step(25);
    assert(harness.leader() != 0);
    NodeId lead = harness.leader();
    std::cout << "Leader: node " << lead << "\n";

    // Warm up.
    for (int i = 0; i < 20; ++i) {
        harness.propose(lead, encodeNew(i + 1, Side::Buy, 10000 + i, 1));
        harness.step(3);
    }

    // Measure: propose + step until committed (end-to-end).
    constexpr int N = 1000;
    std::vector<double> samples;
    samples.reserve(N);

    size_t target = applyCount[lead];  // baseline after warmup
    for (int i = 0; i < N; ++i) {
        uint64_t orderId = 1000 + i;
        auto cmd = encodeNew(orderId, Side::Buy, 10000 + (i % 10), 1);

        auto t0 = Clock::now();
        assert(harness.propose(lead, cmd));

        // Step until the leader has applied it (with safety limit).
        size_t need = target + static_cast<size_t>(i) + 1;
        int steps = 0;
        while (applyCount[lead] < need && steps < 100) {
            harness.step(1);
            steps++;
            // If leader changed, bail.
            if (harness.leader() != lead) break;
        }
        auto t1 = Clock::now();

        double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        samples.push_back(us);
    }

    // Let things settle and verify consistency.
    harness.step(20);
    auto refBids = fsms[1]->bids();
    for (auto id : peers) {
        assert(fsms[id]->bids() == refBids);
    }

    // Stats.
    std::sort(samples.begin(), samples.end());
    double sum = 0;
    for (double s : samples) sum += s;
    double avg = sum / N;
    double p50 = samples[N / 2];
    double p90 = samples[static_cast<int>(N * 0.90)];
    double p99 = samples[static_cast<int>(N * 0.99)];
    double p999 = samples[static_cast<int>(N * 0.999)];
    double maxv = samples.back();
    double minv = samples.front();

    std::cout << "=== LOB Benchmark (replicated order, 3-node, in-process) ===\n";
    std::cout << "  Samples:    " << N << "\n";
    std::cout << "  Average:    " << avg << " us\n";
    std::cout << "  p50:        " << p50 << " us\n";
    std::cout << "  p90:        " << p90 << " us\n";
    std::cout << "  p99:        " << p99 << " us\n";
    std::cout << "  p99.9:      " << p999 << " us\n";
    std::cout << "  Max:        " << maxv << " us\n";
    std::cout << "  Min:        " << minv << " us\n";
    std::cout << "  Throughput: " << static_cast<int>(1e6 / avg) << " ops/sec\n";
    std::cout << "test_lob_benchmark: PASS\n";
    return 0;
}
