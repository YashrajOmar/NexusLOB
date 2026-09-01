#include "protocol/Protocol.h"

#include <cstring>

namespace app {
namespace protocol {

namespace {

void appendU8 (std::string& buf, uint8_t  v) { buf.push_back(static_cast<char>(v)); }
void appendU32(std::string& buf, uint32_t v) {
    for (int i = 0; i < 4; ++i) buf.push_back(static_cast<char>((v >> (i*8)) & 0xFF));
}
void appendStr(std::string& buf, const std::string& s) {
    appendU32(buf, static_cast<uint32_t>(s.size()));
    buf.insert(buf.end(), s.begin(), s.end());
}

bool readU8 (const std::string& buf, size_t& off, uint8_t& out) {
    if (off + 1 > buf.size()) return false;
    out = static_cast<uint8_t>(buf[off]); off += 1; return true;
}
bool readU32(const std::string& buf, size_t& off, uint32_t& out) {
    if (off + 4 > buf.size()) return false;
    out = 0; for (int i = 0; i < 4; ++i) out |= static_cast<uint32_t>(static_cast<uint8_t>(buf[off+i])) << (i*8);
    off += 4; return true;
}
bool readStr(const std::string& buf, size_t& off, std::string& out) {
    uint32_t len;
    if (!readU32(buf, off, len)) return false;
    if (off + len > buf.size()) return false;
    out.assign(buf, off, len);
    off += len; return true;
}

} // namespace

// ============================================================
// Encode client commands → KVStateMachine binary format
// ============================================================

std::string encodeSet(const std::string& key, const std::string& value) {
    std::string buf;
    appendU8(buf, OP_SET);
    appendStr(buf, key);
    appendStr(buf, value);
    return buf;
}

std::string encodeGet(const std::string& key) {
    std::string buf;
    appendU8(buf, OP_GET);
    appendStr(buf, key);
    return buf;
}

std::string encodeDel(const std::string& key) {
    std::string buf;
    appendU8(buf, OP_DEL);
    appendStr(buf, key);
    return buf;
}

// ============================================================
// Decode apply() result → (found, value)
// ============================================================

bool decodeApplyResult(const std::string& bytes,
                       bool& found,
                       std::string& value) {
    size_t off = 0;
    uint8_t f;
    if (!readU8(bytes, off, f)) return false;
    found = (f != 0);
    if (!readStr(bytes, off, value)) return false;
    return true;
}

} // namespace protocol
} // namespace app
