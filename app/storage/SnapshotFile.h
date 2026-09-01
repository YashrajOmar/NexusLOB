#pragma once

#include "raft/Types.h"
#include "raft/RPC.h"   // for Snapshot

#include <string>
#include <vector>
#include <fstream>
#include <mutex>
#include <optional>

namespace app {

// SnapshotFile: persists a single FSM snapshot to disk.
//
// File layout in <dir>/:
//   snapshot.bin — [term:8][index:8][dataLen:4][data bytes...]
//
// A snapshot replaces the front of the log (Section 7). On restart, the
// application loads this file, hands the data to KVStateMachine::restore(),
// then replays log entries above snapshot.index.
class SnapshotFile {
public:
    explicit SnapshotFile(const std::string& dir);

    // Save a snapshot to disk (atomically: write to .tmp, rename to .bin).
    void save(const raft::Snapshot& snap);

    // Load the snapshot from disk. Returns nullopt if none exists.
    std::optional<raft::Snapshot> load();

    // Remove the snapshot file (after the log has caught up past it).
    void remove();

private:
    std::string path_;
    std::mutex  mutex_;
};

} // namespace app
