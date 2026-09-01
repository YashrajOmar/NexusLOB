#include "../app/storage/WriteAheadLog.h"

#include <iostream>
#include <cassert>
#include <filesystem>
#include <string>

using namespace app;
using namespace raft;

namespace {

Entry makeEntry(Term term, Index index, const std::string& data) {
    Entry e;
    e.term  = term;
    e.index = index;
    e.type  = EntryType::Normal;
    e.data.assign(data.begin(), data.end());
    return e;
}

} // namespace

int main() {
    std::string dir = "./test_data_crash";
    // Clean slate.
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    // --- Phase 1: write entries + hardstate ---
    {
        WriteAheadLog wal(dir);

        wal.append({makeEntry(1, 1, "SET x=5"),
                    makeEntry(1, 2, "SET y=9"),
                    makeEntry(1, 3, "SET z=12")});

        HardState hs;
        hs.term   = 1;
        hs.vote   = 1;
        hs.commit = 3;
        wal.saveHardState(hs);

        std::cout << "Phase 1: wrote 3 entries + hardstate, simulating crash...\n";
        // wal destructor closes the file; the crash is "no clean shutdown
        // of the *node*" — but the WAL itself was fsync'd, so disk has it.
    }

    // --- Phase 2: reopen, verify state survived ---
    {
        WriteAheadLog wal2(dir);  // fresh instance, same dir

        // HardState should be intact.
        auto init = wal2.initialState();
        assert(init.hardState.term   == 1);
        assert(init.hardState.vote   == 1);
        assert(init.hardState.commit == 3);
        std::cout << "HardState recovered: term=" << init.hardState.term
                  << " vote=" << init.hardState.vote
                  << " commit=" << init.hardState.commit << "\n";

        // Log entries should be intact.
        assert(wal2.firstIndex() == 1);
        assert(wal2.lastIndex()  == 3);

        auto ents = wal2.entries(1, 4, 1ull << 20);
        assert(ents.size() == 3);
        assert(ents[0].index == 1);
        assert(ents[1].index == 2);
        assert(ents[2].index == 3);

        // Verify data contents.
        std::string d0(ents[0].data.begin(), ents[0].data.end());
        std::string d1(ents[1].data.begin(), ents[1].data.end());
        std::string d2(ents[2].data.begin(), ents[2].data.end());
        assert(d0 == "SET x=5");
        assert(d1 == "SET y=9");
        assert(d2 == "SET z=12");
        std::cout << "Recovered entries: \"" << d0 << "\", \""
                  << d1 << "\", \"" << d2 << "\"\n";
    }

    // --- Phase 3: partial write (crash mid-batch) ---
    {
        WriteAheadLog wal3(dir);
        // Append one entry, then drop the instance (simulate crash).
        // Since append() fsyncs internally, the entry is durable.
        wal3.append({makeEntry(1, 4, "SET w=99")});

        // Reopen: entry 4 should exist (it was fsync'd inside append()).
        WriteAheadLog wal4(dir);
        assert(wal4.lastIndex() == 4);
        auto ents = wal4.entries(4, 5, 1ull << 20);
        assert(ents.size() == 1);
        std::string d(ents[0].data.begin(), ents[0].data.end());
        assert(d == "SET w=99");
        std::cout << "Entry 4 recovered after reopen: \"" << d << "\"\n";
    }

    // Cleanup.
    std::filesystem::remove_all(dir);

    std::cout << "test_wal_crash_recovery: PASS\n";
    return 0;
}
