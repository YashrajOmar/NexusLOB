#include "statemachine/KVStateMachine.h"

#include <cstring>
#include <stdexcept>

namespace app {

namespace {

// Opcodes for the KV command wire format.
constexpr uint8_t OP_SET = 0x01;
constexpr uint8_t OP_GET = 0x02;
constexpr uint8_t OP_DEL = 0x03;

// --- Binary read/write helpers ---

void writeU8 (std::vector<uint8_t>& buf, uint8_t  v) { buf.push_back(v); }
void writeU32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8)  & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}
void writeStr(std::vector<uint8_t>& buf, const std::string& s) {
    writeU32(buf, static_cast<uint32_t>(s.size()));
    buf.insert(buf.end(), s.begin(), s.end());
}

bool readU8 (const std::vector<uint8_t>& buf, size_t& off, uint8_t& out) {
    if (off + 1 > buf.size()) return false;
    out = buf[off]; off += 1; return true;
}
bool readU32(const std::vector<uint8_t>& buf, size_t& off, uint32_t& out) {
    if (off + 4 > buf.size()) return false;
    out = buf[off] | (buf[off+1] << 8) | (buf[off+2] << 16) | (buf[off+3] << 24);
    off += 4; return true;
}
bool readStr(const std::vector<uint8_t>& buf, size_t& off, std::string& out) {
    uint32_t len;
    if (!readU32(buf, off, len)) return false;
    if (off + len > buf.size()) return false;
    out.assign(reinterpret_cast<const char*>(buf.data() + off), len);
    off += len; return true;
}

} // namespace

// ============================================================
// Construction
// ============================================================

KVStateMachine::KVStateMachine() = default;

// ============================================================
// StateMachine interface
// ============================================================

std::vector<uint8_t> KVStateMachine::apply(const std::vector<uint8_t>& data) {
    uint8_t op;
    std::string key, val;
    if (!decode(data, op, key, val)) {
        return {};  // malformed command — return empty
    }

    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<uint8_t> result;
    std::string oldVal;

    switch (op) {
        case OP_SET: {
            auto it = map_.find(key);
            bool existed = (it != map_.end());
            if (existed) oldVal = it->second;
            map_[key] = val;
            // Return previous value: [existed:1][valLen:4][val]
            writeU8(result, existed ? 1 : 0);
            writeStr(result, oldVal);
            break;
        }
        case OP_GET: {
            auto it = map_.find(key);
            bool found = (it != map_.end());
            if (found) oldVal = it->second;
            writeU8(result, found ? 1 : 0);
            writeStr(result, oldVal);
            break;
        }
        case OP_DEL: {
            auto it = map_.find(key);
            bool existed = (it != map_.end());
            if (existed) oldVal = it->second;
            map_.erase(it);
            writeU8(result, existed ? 1 : 0);
            writeStr(result, oldVal);
            break;
        }
        default:
            break;  // unknown opcode — return empty
    }
    return result;
}

std::vector<uint8_t> KVStateMachine::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return serializeMap();
}

void KVStateMachine::restore(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    deserializeMap(data);
}

// ============================================================
// Direct reads (non-replicated, not linearizable)
// ============================================================

bool KVStateMachine::get(const std::string& key, std::string& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = map_.find(key);
    if (it == map_.end()) return false;
    out = it->second;
    return true;
}

bool KVStateMachine::exists(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return map_.find(key) != map_.end();
}

size_t KVStateMachine::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return map_.size();
}

// ============================================================
// Command encoding (used by ClientServer to build proposals)
// ============================================================

std::vector<uint8_t> KVStateMachine::encodeSet(const std::string& k, const std::string& v) {
    std::vector<uint8_t> buf;
    writeU8(buf, OP_SET);
    writeStr(buf, k);
    writeStr(buf, v);
    return buf;
}

std::vector<uint8_t> KVStateMachine::encodeGet(const std::string& k) {
    std::vector<uint8_t> buf;
    writeU8(buf, OP_GET);
    writeStr(buf, k);
    return buf;
}

std::vector<uint8_t> KVStateMachine::encodeDel(const std::string& k) {
    std::vector<uint8_t> buf;
    writeU8(buf, OP_DEL);
    writeStr(buf, k);
    return buf;
}

// ============================================================
// Command decoding
// ============================================================

bool KVStateMachine::decode(const std::vector<uint8_t>& data,
                             uint8_t& op,
                             std::string& key,
                             std::string& val) {
    size_t off = 0;
    if (!readU8(data, off, op)) return false;
    if (!readStr(data, off, key)) return false;
    if (op == OP_SET) {
        if (!readStr(data, off, val)) return false;
    }
    return true;
}

// ============================================================
// Snapshot serialization: [entryCount:4]([keyLen:4][key][valLen:4][val])*
// ============================================================

std::vector<uint8_t> KVStateMachine::serializeMap() const {
    std::vector<uint8_t> buf;
    writeU32(buf, static_cast<uint32_t>(map_.size()));
    for (const auto& [k, v] : map_) {
        writeStr(buf, k);
        writeStr(buf, v);
    }
    return buf;
}

void KVStateMachine::deserializeMap(const std::vector<uint8_t>& data) {
    map_.clear();
    size_t off = 0;
    uint32_t count;
    if (!readU32(data, off, count)) return;
    for (uint32_t i = 0; i < count; ++i) {
        std::string k, v;
        if (!readStr(data, off, k)) return;
        if (!readStr(data, off, v)) return;
        map_[k] = v;
    }
}

} // namespace app
