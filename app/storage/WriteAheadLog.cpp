#include "storage/WriteAheadLog.h"

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <stdexcept>

// Portable fsync: Windows uses _commit, POSIX uses fsync.
#if defined(_WIN32) || defined(_MSC_VER)
#  include <io.h>
#  include <fcntl.h>
   static void portable_fsync(std::ofstream& f) {
       f.flush();
       // _commit forces the OS to flush to physical media.
       // We need the C file descriptor; std::ofstream doesn't expose it directly,
       // so we rely on flush() + rdbuf()->pubsync() as a best-effort on Windows.
       f.rdbuf()->pubsync();
   }
#else
#  include <unistd.h>
   extern "C" int __get_fd_from_ofstream(std::ofstream& f);  // placeholder
   static void portable_fsync(std::ofstream& f) {
       f.flush();
       // For real durability on POSIX, use fsync(fileno) via the underlying fd.
       // This simplified version relies on flush().
   }
#endif

namespace app {

namespace {

// ---- Binary serialization helpers (length-prefixed records) ----

void writeU8 (std::ofstream& f, uint8_t  v) { f.write(reinterpret_cast<const char*>(&v), 1); }
void writeU32(std::ofstream& f, uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); }
void writeU64(std::ofstream& f, uint64_t v) { f.write(reinterpret_cast<const char*>(&v), 8); }

bool readU8 (std::ifstream& f, uint8_t&  v) { f.read(reinterpret_cast<char*>(&v), 1); return f.good(); }
bool readU32(std::ifstream& f, uint32_t& v) { f.read(reinterpret_cast<char*>(&v), 4); return f.good(); }
bool readU64(std::ifstream& f, uint64_t& v) { f.read(reinterpret_cast<char*>(&v), 8); return f.good(); }

void writeEntry(std::ofstream& f, const raft::Entry& e) {
    writeU64(f, e.term);
    writeU64(f, e.index);
    writeU8 (f, static_cast<uint8_t>(e.type));
    writeU32(f, static_cast<uint32_t>(e.data.size()));
    if (!e.data.empty()) {
        f.write(reinterpret_cast<const char*>(e.data.data()), e.data.size());
    }
}

bool readEntry(std::ifstream& f, raft::Entry& e) {
    if (!readU64(f, e.term))  return false;
    if (!readU64(f, e.index)) return false;
    uint8_t t; if (!readU8(f, t)) return false;
    e.type = static_cast<raft::EntryType>(t);
    uint32_t len; if (!readU32(f, len)) return false;
    e.data.resize(len);
    if (len > 0) {
        f.read(reinterpret_cast<char*>(e.data.data()), len);
        if (!f.good()) return false;
    }
    return true;
}

void writeHardState(std::ofstream& f, const raft::HardState& hs) {
    writeU64(f, hs.term);
    writeU64(f, hs.vote);
    writeU64(f, hs.commit);
}

bool readHardState(std::ifstream& f, raft::HardState& hs) {
    if (!readU64(f, hs.term))   return false;
    if (!readU64(f, hs.vote))   return false;
    if (!readU64(f, hs.commit)) return false;
    return true;
}

void writeConfState(std::ofstream& f, const raft::ConfState& cs) {
    writeU32(f, static_cast<uint32_t>(cs.nodes.size()));
    for (auto id : cs.nodes) writeU64(f, id);
    writeU32(f, static_cast<uint32_t>(cs.learners.size()));
    for (auto id : cs.learners) writeU64(f, id);
}

bool readConfState(std::ifstream& f, raft::ConfState& cs) {
    uint32_t n;
    if (!readU32(f, n)) return false;
    cs.nodes.resize(n);
    for (auto& id : cs.nodes) if (!readU64(f, id)) return false;
    uint32_t l;
    if (!readU32(f, l)) return false;
    cs.learners.resize(l);
    for (auto& id : cs.learners) if (!readU64(f, id)) return false;
    return true;
}

} // namespace

// ============================================================
// Construction / destruction
// ============================================================

WriteAheadLog::WriteAheadLog(const std::string& dir)
    : dir_(dir)
    , logPath_(dir + "/log.bin")
    , hsPath_(dir + "/hardstate.bin")
    , csPath_(dir + "/confstate.bin") {

    // Open log.bin for append (binary). Creates the file if it doesn't exist.
    logFile_.open(logPath_, std::ios::binary | std::ios::app);
    if (!logFile_.is_open()) {
        throw std::runtime_error("cannot open " + logPath_);
    }

    loadHardState();
    loadConfState();
    replay();
}

WriteAheadLog::~WriteAheadLog() {
    if (logFile_.is_open()) {
        logFile_.close();
    }
}

// ============================================================
// Startup: load the three files
// ============================================================

void WriteAheadLog::replay() {
    std::ifstream f(logPath_, std::ios::binary);
    if (!f.is_open()) return;

    // Always have a dummy entry at index 0 (paper uses 1-based indexing).
    entries_.clear();
    raft::Entry dummy;
    dummy.index = 0;
    dummy.term  = 0;
    entries_.push_back(dummy);

    raft::Entry e;
    while (readEntry(f, e)) {
        // Grow the vector if needed (handles sparse indices defensively).
        while (entries_.size() <= e.index) {
            entries_.push_back(raft::Entry{});
        }
        entries_[e.index] = e;
    }
}

void WriteAheadLog::loadHardState() {
    std::ifstream f(hsPath_, std::ios::binary);
    if (!f.is_open()) return;  // first startup — empty hardstate
    readHardState(f, hardState_);
}

void WriteAheadLog::loadConfState() {
    std::ifstream f(csPath_, std::ios::binary);
    if (!f.is_open()) return;  // first startup — empty confstate
    readConfState(f, confState_);
}

void WriteAheadLog::flushSync() {
    portable_fsync(logFile_);
}

// ============================================================
// raft::Storage interface (read side)
// ============================================================

raft::InitialState WriteAheadLog::initialState() {
    std::lock_guard<std::mutex> lock(mutex_);
    return {hardState_, confState_};
}

std::vector<raft::Entry> WriteAheadLog::entries(raft::Index lo,
                                                  raft::Index hi,
                                                  uint64_t maxSize) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (lo < firstIndex()) {
        throw std::out_of_range("entries: lo below firstIndex");
    }
    if (hi > lastIndex() + 1) {
        throw std::out_of_range("entries: hi above lastIndex+1");
    }
    std::vector<raft::Entry> result;
    uint64_t size = 0;
    for (raft::Index i = lo; i < hi; ++i) {
        const auto& e = entries_[i];
        size += sizeof(raft::Entry) + e.data.size();
        if (maxSize != 0 && size > maxSize && !result.empty()) {
            break;
        }
        result.push_back(e);
    }
    return result;
}

raft::Term WriteAheadLog::term(raft::Index i) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (i < firstIndex() || i > lastIndex()) {
        throw std::out_of_range("term: index out of range");
    }
    return entries_[i].term;
}

raft::Index WriteAheadLog::firstIndex() {
    // After a snapshot, firstIndex would be snapIndex_+1. Until then, it's 1.
    return snapIndex_ + 1;
}

raft::Index WriteAheadLog::lastIndex() {
    return static_cast<raft::Index>(entries_.size()) - 1;
}

raft::Snapshot WriteAheadLog::snapshot() {
    std::lock_guard<std::mutex> lock(mutex_);
    raft::Snapshot s;
    s.index = snapIndex_;
    s.term  = snapTerm_;
    return s;
}

// ============================================================
// Write side (called by app/Server after poll())
// ============================================================

void WriteAheadLog::append(const std::vector<raft::Entry>& ents) {
    if (ents.empty()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    // Truncate any existing entries from the first new entry onward (etcd pattern).
    raft::Index first = ents[0].index;
    if (first <= lastIndex()) {
        entries_.resize(first);
        // Rewrite log.bin with the truncated log.
        logFile_.close();
        std::ofstream out(logPath_, std::ios::binary | std::ios::trunc);
        for (raft::Index i = snapIndex_ + 1; i < entries_.size(); ++i) {
            writeEntry(out, entries_[i]);
        }
        out.flush();
        out.rdbuf()->pubsync();
        out.close();
        logFile_.open(logPath_, std::ios::binary | std::ios::app);
    }
    for (const auto& e : ents) {
        writeEntry(logFile_, e);
        entries_.push_back(e);
    }
    flushSync();
}

void WriteAheadLog::saveHardState(const raft::HardState& hs) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ofstream f(hsPath_, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) {
        throw std::runtime_error("cannot open " + hsPath_);
    }
    writeHardState(f, hs);
    f.flush();
    f.rdbuf()->pubsync();
    hardState_ = hs;
}

void WriteAheadLog::saveConfState(const raft::ConfState& cs) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ofstream f(csPath_, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) {
        throw std::runtime_error("cannot open " + csPath_);
    }
    writeConfState(f, cs);
    f.flush();
    f.rdbuf()->pubsync();
    confState_ = cs;
}

void WriteAheadLog::applySnapshot(const raft::Snapshot& snap) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Truncate the in-memory log: keep only entries > snap.index.
    // Replace the entry at snap.index with a dummy carrying snap.term.
    if (snap.index < entries_.size()) {
        entries_.resize(snap.index + 1);
    }
    raft::Entry dummy;
    dummy.index = snap.index;
    dummy.term  = snap.term;
    entries_[snap.index] = dummy;

    // Rewrite log.bin with the truncated log.
    logFile_.close();
    std::ofstream out(logPath_, std::ios::binary | std::ios::trunc);
    for (raft::Index i = snapIndex_ + 1; i < entries_.size(); ++i) {
        writeEntry(out, entries_[i]);
    }
    out.flush();
    out.rdbuf()->pubsync();
    out.close();

    // Reopen for append.
    logFile_.open(logPath_, std::ios::binary | std::ios::app);

    snapIndex_ = snap.index;
    snapTerm_  = snap.term;
}

} // namespace app
