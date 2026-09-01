#include "storage/SnapshotFile.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <filesystem>

#if defined(_WIN32) || defined(_MSC_VER)
#  include <windows.h>
#else
#  include <unistd.h>
#endif

namespace app {

namespace {

void writeU8 (std::ofstream& f, uint8_t  v) { f.write(reinterpret_cast<const char*>(&v), 1); }
void writeU32(std::ofstream& f, uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); }
void writeU64(std::ofstream& f, uint64_t v) { f.write(reinterpret_cast<const char*>(&v), 8); }

bool readU8 (std::ifstream& f, uint8_t&  v) { f.read(reinterpret_cast<char*>(&v), 1); return f.good(); }
bool readU32(std::ifstream& f, uint32_t& v) { f.read(reinterpret_cast<char*>(&v), 4); return f.good(); }
bool readU64(std::ifstream& f, uint64_t& v) { f.read(reinterpret_cast<char*>(&v), 8); return f.good(); }

bool renameFile(const std::string& from, const std::string& to) {
#if defined(_WIN32) || defined(_MSC_VER)
    // MoveFileExA with MOVEFILE_REPLACE_EXISTING atomically replaces.
    return MoveFileExA(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return ::rename(from.c_str(), to.c_str()) == 0;
#endif
}

} // namespace

SnapshotFile::SnapshotFile(const std::string& dir)
    : path_(dir + "/snapshot.bin") {}

void SnapshotFile::save(const raft::Snapshot& snap) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Write to a .tmp file first so a crash mid-write doesn't corrupt the
    // existing valid snapshot.
    std::string tmp = path_ + ".tmp";
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) {
        throw std::runtime_error("cannot open " + tmp);
    }

    writeU64(f, snap.term);
    writeU64(f, snap.index);
    writeU32(f, static_cast<uint32_t>(snap.data.size()));
    if (!snap.data.empty()) {
        f.write(reinterpret_cast<const char*>(snap.data.data()), snap.data.size());
    }

    f.flush();
    f.rdbuf()->pubsync();
    f.close();

    // Atomic rename: either the new snapshot is fully in place, or the old
    // one remains intact. No half-written state is ever visible.
    if (!renameFile(tmp, path_)) {
        // Best-effort cleanup of the .tmp file on failure.
        std::remove(tmp.c_str());
        throw std::runtime_error("rename failed: " + tmp + " -> " + path_);
    }
}

std::optional<raft::Snapshot> SnapshotFile::load() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::ifstream f(path_, std::ios::binary);
    if (!f.is_open()) {
        return std::nullopt;  // no snapshot exists
    }

    raft::Snapshot snap;
    if (!readU64(f, snap.term))  return std::nullopt;
    if (!readU64(f, snap.index)) return std::nullopt;

    uint32_t len;
    if (!readU32(f, len)) return std::nullopt;
    snap.data.resize(len);
    if (len > 0) {
        f.read(reinterpret_cast<char*>(snap.data.data()), len);
        if (!f.good()) return std::nullopt;
    }

    return snap;
}

void SnapshotFile::remove() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::remove(path_.c_str());
}

} // namespace app
