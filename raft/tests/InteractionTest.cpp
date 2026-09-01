#include "raft/RawNode.h"
#include "raft/Storage.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

namespace raft {
namespace test {

// ============================================================
// MemoryStorage: in-memory Storage for tests (etcd's NewMemoryStorage)
// ============================================================

class MemoryStorageError : public std::exception {
public:
    explicit MemoryStorageError(const char* what) : what_(what) {}
    const char* what() const noexcept override { return what_; }
private:
    const char* what_;
};

class MemoryStorage : public Storage {
public:
    MemoryStorage() {
        // Dummy entry at index 0 (paper uses 1-based indexing).
        Entry dummy;
        dummy.index = 0;
        dummy.term  = 0;
        entries_.push_back(dummy);
    }

    void setInitialState(const HardState& hs, const ConfState& cs) {
        hardState_ = hs;
        confState_ = cs;
    }

    InitialState initialState() override {
        return {hardState_, confState_};
    }

    std::vector<Entry> entries(Index lo, Index hi, uint64_t maxSize) override {
        if (lo < firstIndex()) {
            throw MemoryStorageError("compacted");
        }
        if (hi > lastIndex() + 1) {
            hi = lastIndex() + 1;
        }
        std::vector<Entry> result;
        for (Index i = lo; i < hi; ++i) {
            result.push_back(entries_[i]);
        }
        return result;
    }

    Term term(Index i) override {
        if (i < firstIndex()) {
            throw MemoryStorageError("compacted");
        }
        if (i > lastIndex()) {
            throw MemoryStorageError("unavailable");
        }
        return entries_[i].term;
    }

    Index firstIndex() override { return 1; }
    Index lastIndex() override  { return static_cast<Index>(entries_.size()) - 1; }

    Snapshot snapshot() override {
        Snapshot s;
        s.index = snapshotIndex_;
        s.term  = snapshotTerm_;
        return s;
    }

    // Test helpers: append entries (simulating persistence).
    void append(const std::vector<Entry>& ents) {
        for (const auto& e : ents) {
            while (entries_.size() <= e.index) {
                entries_.push_back(Entry{});
            }
            entries_[e.index] = e;
        }
    }

    void applySnapshot(Index idx, Term t) {
        snapshotIndex_ = idx;
        snapshotTerm_  = t;
    }

private:
    std::vector<Entry> entries_;
    HardState hardState_{};
    ConfState  confState_{};
    Index snapshotIndex_ = 0;
    Term  snapshotTerm_  = 0;
};

// ============================================================
// Test harness: drives a RawNode with scripted actions
// ============================================================

struct Action {
    enum Type { Tick, Step, Propose, ExpectEntries, ExpectRole, ExpectTerm };
    Type type;
    uint32_t ticks = 1;
    Message msg;
    std::vector<uint8_t> data;
    size_t expectEntries = 0;
    Role expectRole = Role::Follower;
    Term expectTerm = 0;
};

struct TestCase {
    std::string name;
    Config      config;
    std::vector<Action> steps;
};

void runCase(const TestCase& tc) {
    MemoryStorage storage;
    Config c = tc.config;
    c.storage = &storage;

    ConfState cs;
    cs.nodes = c.peers;
    HardState hs;
    storage.setInitialState(hs, cs);

    RawNode node(c);

    std::cout << "  Running: " << tc.name << "\n";

    for (const auto& act : tc.steps) {
        switch (act.type) {
            case Action::Tick: {
                for (uint32_t i = 0; i < act.ticks; ++i) {
                    node.tick();
                }
                break;
            }
            case Action::Step: {
                node.step(act.msg);
                break;
            }
            case Action::Propose: {
                bool ok = node.propose(act.data);
                (void)ok;
                break;
            }
            case Action::ExpectEntries: {
                Ready rd = node.poll();
                assert(rd.entries.size() == act.expectEntries);
                node.advance();
                break;
            }
            case Action::ExpectRole: {
                assert(node.role() == act.expectRole);
                break;
            }
            case Action::ExpectTerm: {
                assert(node.term() == act.expectTerm);
                break;
            }
        }
    }
    std::cout << "    PASS\n";
}

// ============================================================
// Test cases
// ============================================================

int runAll() {
    // --- Case 1: single-node cluster elects itself and commits ---
    {
        TestCase tc;
        tc.name = "single-node election + commit";
        tc.config.id = 1;
        tc.config.electionTick = 10;
        tc.config.heartbeatTick = 1;
        tc.config.peers = {1};

        tc.steps.push_back({Action::Tick, 25});
        tc.steps.push_back({Action::ExpectRole, 0, {}, {}, 0, Role::Leader, 0});
        tc.steps.push_back({Action::ExpectTerm, 0, {}, {}, 0, Role::Follower, 1});

        tc.steps.push_back({Action::Propose, 0, {}, {'x'}, 0, Role::Follower, 0});
        tc.steps.push_back({Action::ExpectEntries, 0, {}, {}, 1, Role::Follower, 0});

        runCase(tc);
    }

    // --- Case 2: follower starts as follower ---
    {
        TestCase tc;
        tc.name = "follower starts as follower";
        tc.config.id = 2;
        tc.config.electionTick = 10;
        tc.config.heartbeatTick = 1;
        tc.config.peers = {1, 2};

        tc.steps.push_back({Action::ExpectRole, 0, {}, {}, 0, Role::Follower, 0});

        runCase(tc);
    }

    std::cout << "All interaction tests passed.\n";
    return 0;
}

} // namespace test
} // namespace raft

int main() {
    return raft::test::runAll();
}
