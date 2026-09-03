#pragma once

#include "raft/Storage.h"
#include "raft/RPC.h"

#include <string>
#include <vector>
#include <fstream>
#include <mutex>

namespace app {

// WriteAheadLog: disk-backed implementation of raft::Storage.
//
// File layout in <dir>/:
//   log.bin         — append-only length-prefixed Entry records
//   hardstate.bin   — single HardState record (rewritten in place)
//   confstate.bin   — single ConfState record (rewritten in place)
//
// On startup, log.bin is replayed to rebuild the in-memory entries_ vector.
// HardState and ConfState are read directly from their files.
//
// Thread safety: internal mutex serializes all disk operations. The raft
// algorithm is single-threaded by contract; this mutex protects against
// the application's checkpoint/snapshot thread if one exists later.
class WriteAheadLog : public raft::Storage {
public:
    explicit WriteAheadLog(const std::string& dir);
    ~WriteAheadLog() override;

    // --- raft::Storage interface (read side) ---

    raft::InitialState initialState() override;
    std::vector<raft::Entry> entries(raft::Index lo,
                                      raft::Index hi,
                                      uint64_t maxSize) override;
    raft::Term term(raft::Index i) override;
    raft::Index firstIndex() override;
    raft::Index lastIndex() override;
    raft::Snapshot snapshot() override;

    // --- Write side (called by app/Server after poll()) ---

    // Append entries to log.bin WITHOUT fsync (group commit).
    // Caller batches multiple appends, then calls sync() once.
    void appendNoSync(const std::vector<raft::Entry>& ents);

    // fsync the log file. Called once after a batch of appendNoSync calls.
    void sync();

    // Append entries to log.bin and fsync immediately (legacy, use appendNoSync + sync).
    void append(const std::vector<raft::Entry>& ents);

    // Overwrite hardstate.bin and fsync. Used to persist Ready.hardState.
    void saveHardState(const raft::HardState& hs);

    // Overwrite confstate.bin and fsync. Used when membership changes.
    void saveConfState(const raft::ConfState& cs);

    // Apply a snapshot: truncate log.bin up to snap.index, save snapshot.
    void applySnapshot(const raft::Snapshot& snap);

private:
    void replay();          // load log.bin into entries_
    void loadHardState();   // load hardstate.bin
    void loadConfState();   // load confstate.bin
    void flushSync();       // flush + fsync the log file

    std::string   dir_;
    std::string   logPath_;
    std::string   hsPath_;
    std::string   csPath_;

    std::ofstream logFile_;      // open for append
    std::mutex    mutex_;

    // In-memory mirror of what's on disk.
    std::vector<raft::Entry> entries_;
    raft::HardState          hardState_;
    raft::ConfState          confState_;

    // Snapshot tracking.
    raft::Index snapIndex_ = 0;
    raft::Term  snapTerm_  = 0;
};

} // namespace app
