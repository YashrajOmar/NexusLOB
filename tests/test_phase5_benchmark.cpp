#include "ClusterHarness.h"
#include "../app/statemachine/MapOrderBook.h"
#include "../app/statemachine/LOBStateMachine.h"
#include "../app/protocol/OrderProtocol.h"

#include <iostream>
#include <cassert>
#include <vector>
#include <memory>
#include <chrono>
#include <algorithm>

using namespace raft;
using namespace raft::test;
using namespace app;
using namespace app::order_protocol;
using Clock = std::chrono::high_resolution_clock;

int main() {
    // --- Setup: 3 symbols, each with its own 3-node cluster ---
    std::vector<NodeId> peers = {1, 2, 3};
    std::vector<std::string> symbols = {"AAPL", "GOOG", "MSFT"};

    std::map<std::string, std::unique_ptr<ClusterHarness>> harnesses;
    std::map<std::string, std::map<NodeId, std::unique_ptr<LOBStateMachine>>> fsms;
    std::map<std::string, size_t> applyCount;

    for (const auto& sym : symbols) {
        harnesses[sym] = std::make_unique<ClusterHarness>(peers);
        for (auto id : peers) {
            fsms[sym][id] = std::make_unique<LOBStateMachine>(
                std::make_unique<MapOrderBook>(sym));
            applyCount[sym + std::to_string(id)] = 0;
        }
        harnesses[sym]->setApplyCallback(
            [&](NodeId id, const std::vector<Entry>& entries) {
                for (const auto& e : entries) {
                    fsms[sym][id]->apply(e.data);
                    applyCount[sym + std::to_string(id)]++;
                }
            });
    }

    // Elect leaders for all symbols.
    for (auto& [sym, h] : harnesses) {
        for (int attempt = 0; attempt < 10 && h->leader() == 0; ++attempt)
            h->step(25);
        assert(h->leader() != 0);
    }
    std::cout << "Leaders elected for all " << symbols.size() << " symbols\n";

    // Warm up.
    for (const auto& sym : symbols) {
        auto& h = *harnesses[sym];
        for (int i = 0; i < 10; ++i) {
            h.propose(h.leader(), encodeNew(i + 1, Side::Buy, 10000 + i, 1));
            h.step(3);
        }
    }

    // --- Benchmark 1: Single symbol (baseline) ---
    constexpr int N = 300;
    std::vector<double> singleSamples;
    singleSamples.reserve(N);

    auto& h1 = *harnesses["AAPL"];
    NodeId lead1 = h1.leader();
    size_t baseline1 = applyCount["AAPL" + std::to_string(lead1)];

    for (int i = 0; i < N; ++i) {
        auto cmd = encodeNew(1000 + i, Side::Buy, 10000 + (i % 10), 1);
        auto t0 = Clock::now();
        h1.propose(lead1, cmd);
        size_t need = baseline1 + i + 1;
        int steps = 0;
        while (applyCount["AAPL" + std::to_string(lead1)] < need && steps < 100) {
            h1.step(1); steps++;
            if (h1.leader() != lead1) break;
        }
        auto t1 = Clock::now();
        singleSamples.push_back(
            std::chrono::duration<double, std::micro>(t1 - t0).count());
    }

    // --- Benchmark 2: Sharded (3 symbols, interleaved) ---
    std::vector<double> shardedSamples;
    shardedSamples.reserve(N);

    std::map<std::string, NodeId> leads;
    std::map<std::string, size_t> baselines;
    for (const auto& sym : symbols) {
        leads[sym] = harnesses[sym]->leader();
        baselines[sym] = applyCount[sym + std::to_string(leads[sym])];
    }

    for (int i = 0; i < N; ++i) {
        const std::string& sym = symbols[i % symbols.size()];
        auto& h = *harnesses[sym];
        NodeId lead = leads[sym];
        auto cmd = encodeNew(2000 + i, Side::Buy, 10000 + (i % 10), 1);
        auto t0 = Clock::now();
        h.propose(lead, cmd);
        size_t need = baselines[sym] + i / symbols.size() + 1;
        int steps = 0;
        while (applyCount[sym + std::to_string(lead)] < need && steps < 100) {
            // Step all symbols (parallel processing).
            for (auto& [s, sh] : harnesses) sh->step(1);
            steps++;
            if (h.leader() != lead) break;
        }
        auto t1 = Clock::now();
        shardedSamples.push_back(
            std::chrono::duration<double, std::micro>(t1 - t0).count());
    }

    // Verify consistency.
    for (const auto& sym : symbols) {
        for (int s = 0; s < 20; ++s) {
            for (auto& [s2, h] : harnesses) h->step(1);
        }
        auto refBids = fsms[sym][1]->bids();
        for (auto id : peers) assert(fsms[sym][id]->bids() == refBids);
    }

    // --- Stats ---
    auto stats = [](std::vector<double>& s, const char* label) {
        std::sort(s.begin(), s.end());
        double sum = 0;
        for (double v : s) sum += v;
        double avg = sum / s.size();
        double p50 = s[s.size() / 2];
        double p99 = s[static_cast<int>(s.size() * 0.99)];
        std::cout << "  " << label << ":\n";
        std::cout << "    avg=" << avg << " us  p50=" << p50
                  << " us  p99=" << p99
                  << " us  throughput=" << (int)(1e6 / avg) << " ops/sec\n";
    };

    std::cout << "\n=== Phase 5 Benchmark: Sharding ===\n\n";
    stats(singleSamples,  "Single symbol  (1 group)");
    stats(shardedSamples, "Sharded        (3 groups, interleaved)");

    std::cout << "\ntest_phase5_benchmark: PASS\n";
    return 0;
}
