#include "ClusterHarness.h"
#include "../app/statemachine/MapOrderBook.h"
#include "../app/statemachine/SkipListOrderBook.h"
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
        fsms[id] = std::make_unique<LOBStateMachine>(
            std::make_unique<MapOrderBook>("LOB"));
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

    size_t baseline = applyCount[lead];

    // --- Benchmark 1: Single propose (Phase 2 baseline) ---
    constexpr int N1 = 500;
    std::vector<double> singleSamples;
    singleSamples.reserve(N1);
    for (int i = 0; i < N1; ++i) {
        auto cmd = encodeNew(1000 + i, Side::Buy, 10000 + (i % 10), 1);
        auto t0 = Clock::now();
        harness.propose(lead, cmd);
        size_t need = baseline + i + 1;
        int steps = 0;
        while (applyCount[lead] < need && steps < 100) {
            harness.step(1); steps++;
            if (harness.leader() != lead) break;
        }
        auto t1 = Clock::now();
        singleSamples.push_back(
            std::chrono::duration<double, std::micro>(t1 - t0).count());
    }

    // Verify consistency.
    harness.step(20);
    auto refBids = fsms[1]->bids();
    for (auto id : peers) assert(fsms[id]->bids() == refBids);

    // --- Benchmark 2: Batch propose (Phase 4 group commit) ---
    constexpr int BATCH = 10;
    constexpr int N2 = 500;
    std::vector<double> batchSamples;
    batchSamples.reserve(N2 / BATCH);

    size_t batchBaseline = applyCount[lead];
    for (int i = 0; i < N2; i += BATCH) {
        auto t0 = Clock::now();
        // Propose BATCH orders, then step once (group commit).
        for (int j = 0; j < BATCH && (i + j) < N2; ++j) {
            harness.propose(lead, encodeNew(2000 + i + j, Side::Buy, 10000 + ((i+j) % 10), 1));
        }
        // Step until all applied.
        size_t need = batchBaseline + i + BATCH;
        int steps = 0;
        while (applyCount[lead] < need && steps < 100) {
            harness.step(1); steps++;
            if (harness.leader() != lead) break;
        }
        auto t1 = Clock::now();
        double us = std::chrono::duration<double, std::micro>(t1 - t0).count() / BATCH;
        batchSamples.push_back(us);
    }

    // Verify consistency.
    harness.step(20);
    refBids = fsms[1]->bids();
    for (auto id : peers) assert(fsms[id]->bids() == refBids);

    // --- Stats ---
    auto stats = [](std::vector<double>& s, const char* label) {
        std::sort(s.begin(), s.end());
        double sum = 0;
        for (double v : s) sum += v;
        double avg = sum / s.size();
        double p50 = s[s.size() / 2];
        double p99 = s[static_cast<int>(s.size() * 0.99)];
        double p999 = s[static_cast<int>(s.size() * 0.999)];
        std::cout << "  " << label << ":\n";
        std::cout << "    avg=" << avg << " us  p50=" << p50 << " us  p99=" << p99
                  << " us  p99.9=" << p999 << " us  throughput=" << (int)(1e6/avg) << " ops/sec\n";
    };

    std::cout << "\n=== Phase 4 Benchmark: Group Commit ===\n\n";
    stats(singleSamples, "Single propose (Phase 2)");
    stats(batchSamples,  "Batch propose  (Phase 4)");

    std::cout << "\ntest_phase4_benchmark: PASS\n";
    return 0;
}
