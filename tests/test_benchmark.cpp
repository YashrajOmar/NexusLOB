#include "ClusterHarness.h"

#include <iostream>
#include <cassert>
#include <chrono>
#include <algorithm>
#include <vector>
#include <cmath>

using namespace raft;
using namespace raft::test;
using Clock = std::chrono::high_resolution_clock;

int main() {
    std::vector<NodeId> peers = {1, 2, 3};
    ClusterHarness cluster(peers);

    // Elect a leader.
    cluster.step(25);
    NodeId lead = cluster.leader();
    assert(lead != 0);
    std::cout << "Leader: node " << lead << "\n";

    // Warm up (first proposals are slower — election settle, etc.).
    for (int i = 0; i < 10; ++i) {
        cluster.propose(lead, {'w'});
        cluster.step(3);
    }

    // Measure 1,000 proposals.
    constexpr int N = 1000;
    std::vector<double> latencies;
    latencies.reserve(N);

    for (int i = 0; i < N; ++i) {
        std::vector<uint8_t> data = {
            static_cast<uint8_t>('a' + (i % 26)),
            static_cast<uint8_t>('0' + (i % 10))
        };

        auto start = Clock::now();
        bool ok = cluster.propose(lead, data);
        cluster.step(2);  // let it replicate + commit
        auto end = Clock::now();

        assert(ok);
        double us = std::chrono::duration<double, std::micro>(end - start).count();
        latencies.push_back(us);
    }

    // Sort for percentiles.
    std::sort(latencies.begin(), latencies.end());

    auto pct = [&](double p) -> double {
        size_t idx = static_cast<size_t>(std::ceil(p / 100.0 * N)) - 1;
        return latencies[std::min(idx, static_cast<size_t>(N - 1))];
    };

    double sum = 0;
    for (auto l : latencies) sum += l;
    double avg = sum / N;

    std::cout << "=== Benchmark Results (in-process, no network/disk) ===\n";
    std::cout << "  Samples:    " << N << "\n";
    std::cout << "  Average:    " << avg << " us\n";
    std::cout << "  p50 (median): " << pct(50) << " us\n";
    std::cout << "  p90:         " << pct(90) << " us\n";
    std::cout << "  p99:         " << pct(99) << " us\n";
    std::cout << "  p99.9:       " << pct(99.9) << " us\n";
    std::cout << "  Max:         " << latencies.back() << " us\n";
    std::cout << "  Min:         " << latencies.front() << " us\n";

    double totalMs = sum / 1000.0;
    double throughput = N / (totalMs / 1000.0);
    std::cout << "  Throughput:  " << static_cast<int>(throughput) << " ops/sec\n";

    // Sanity: p50 should be under 10ms (generous bound for in-process).
    if (pct(50) > 10000.0) {
        std::cerr << "WARN: p50 > 10ms — algorithm may have a stall\n";
    }

    std::cout << "test_benchmark: PASS\n";
    return 0;
}
