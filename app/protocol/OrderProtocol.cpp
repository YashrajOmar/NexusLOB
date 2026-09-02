#include "protocol/OrderProtocol.h"

namespace app {
namespace order_protocol {

namespace {

// --- Little-endian binary helpers ---

void putU8 (std::vector<uint8_t>& buf, size_t& off, uint8_t v) {
    if (off + 1 > buf.size()) buf.resize(off + 1);
    buf[off] = v; off += 1;
}
void putU32(std::vector<uint8_t>& buf, size_t& off, uint32_t v) {
    if (off + 4 > buf.size()) buf.resize(off + 4);
    for (int i = 0; i < 4; ++i) { buf[off + i] = static_cast<uint8_t>((v >> (i*8)) & 0xFF); }
    off += 4;
}
void putU64(std::vector<uint8_t>& buf, size_t& off, uint64_t v) {
    if (off + 8 > buf.size()) buf.resize(off + 8);
    for (int i = 0; i < 8; ++i) { buf[off + i] = static_cast<uint8_t>((v >> (i*8)) & 0xFF); }
    off += 8;
}
void putI64(std::vector<uint8_t>& buf, size_t& off, int64_t v) {
    putU64(buf, off, static_cast<uint64_t>(v));
}

bool getU8 (const std::vector<uint8_t>& buf, size_t& off, uint8_t& out) {
    if (off + 1 > buf.size()) return false;
    out = buf[off]; off += 1; return true;
}
bool getU32(const std::vector<uint8_t>& buf, size_t& off, uint32_t& out) {
    if (off + 4 > buf.size()) return false;
    out = 0; for (int i = 0; i < 4; ++i) out |= static_cast<uint32_t>(buf[off+i]) << (i*8);
    off += 4; return true;
}
bool getU64(const std::vector<uint8_t>& buf, size_t& off, uint64_t& out) {
    if (off + 8 > buf.size()) return false;
    out = 0; for (int i = 0; i < 8; ++i) out |= static_cast<uint64_t>(buf[off+i]) << (i*8);
    off += 8; return true;
}
bool getI64(const std::vector<uint8_t>& buf, size_t& off, int64_t& out) {
    uint64_t u;
    if (!getU64(buf, off, u)) return false;
    out = static_cast<int64_t>(u); return true;
}

} // namespace

// ============================================================
// Command encoding
// ============================================================

std::vector<uint8_t> encodeNew(uint64_t orderId, Side side,
                               int64_t price, int64_t quantity) {
    std::vector<uint8_t> buf(1 + 8 + 1 + 8 + 8);
    size_t off = 0;
    putU8 (buf, off, OP_NEW);
    putU64(buf, off, orderId);
    putU8 (buf, off, static_cast<uint8_t>(side));
    putI64(buf, off, price);
    putI64(buf, off, quantity);
    return buf;
}

std::vector<uint8_t> encodeCancel(uint64_t orderId) {
    std::vector<uint8_t> buf(1 + 8);
    size_t off = 0;
    putU8 (buf, off, OP_CXL);
    putU64(buf, off, orderId);
    return buf;
}

std::vector<uint8_t> encodeModify(uint64_t orderId,
                                  int64_t newPrice, int64_t newQuantity) {
    std::vector<uint8_t> buf(1 + 8 + 8 + 8);
    size_t off = 0;
    putU8 (buf, off, OP_MOD);
    putU64(buf, off, orderId);
    putI64(buf, off, newPrice);
    putI64(buf, off, newQuantity);
    return buf;
}

// ============================================================
// Command decoding
// ============================================================

bool decodeNew(const std::vector<uint8_t>& data,
               uint64_t& orderId, Side& side,
               int64_t& price, int64_t& quantity) {
    size_t off = 0;
    uint8_t op;
    if (!getU8(data, off, op) || op != OP_NEW) return false;
    if (!getU64(data, off, orderId)) return false;
    uint8_t s;
    if (!getU8(data, off, s)) return false;
    if (s > 1) return false;
    side = static_cast<Side>(s);
    if (!getI64(data, off, price)) return false;
    if (!getI64(data, off, quantity)) return false;
    return true;
}

bool decodeCancel(const std::vector<uint8_t>& data, uint64_t& orderId) {
    size_t off = 0;
    uint8_t op;
    if (!getU8(data, off, op) || op != OP_CXL) return false;
    if (!getU64(data, off, orderId)) return false;
    return true;
}

bool decodeModify(const std::vector<uint8_t>& data,
                  uint64_t& orderId, int64_t& newPrice, int64_t& newQuantity) {
    size_t off = 0;
    uint8_t op;
    if (!getU8(data, off, op) || op != OP_MOD) return false;
    if (!getU64(data, off, orderId)) return false;
    if (!getI64(data, off, newPrice)) return false;
    if (!getI64(data, off, newQuantity)) return false;
    return true;
}

// ============================================================
// Apply-result encoding
// ============================================================

std::vector<uint8_t> encodeNewResult(const std::vector<Fill>& fills,
                                     bool rested, int64_t remainingQty) {
    // [ok:1][fillCount:4](fills)*[rested:1][remainingQty:8]
    size_t size = 1 + 4 + fills.size() * (8+8+8+8+1) + 1 + 8;
    std::vector<uint8_t> buf(size);
    size_t off = 0;
    putU8 (buf, off, 1);  // ok
    putU32(buf, off, static_cast<uint32_t>(fills.size()));
    for (const auto& f : fills) {
        putU64(buf, off, f.makerOrderId);
        putU64(buf, off, f.takerOrderId);
        putI64(buf, off, f.price);
        putI64(buf, off, f.quantity);
        putU8 (buf, off, static_cast<uint8_t>(f.takerSide));
    }
    putU8 (buf, off, rested ? 1 : 0);
    putI64(buf, off, remainingQty);
    return buf;
}

bool decodeNewResult(const std::vector<uint8_t>& data,
                     std::vector<Fill>& fills, bool& rested,
                     int64_t& remainingQty) {
    size_t off = 0;
    uint8_t ok;
    if (!getU8(data, off, ok) || !ok) return false;
    uint32_t fillCount;
    if (!getU32(data, off, fillCount)) return false;
    fills.clear();
    fills.reserve(fillCount);
    for (uint32_t i = 0; i < fillCount; ++i) {
        Fill f;
        if (!getU64(data, off, f.makerOrderId)) return false;
        if (!getU64(data, off, f.takerOrderId)) return false;
        if (!getI64(data, off, f.price)) return false;
        if (!getI64(data, off, f.quantity)) return false;
        uint8_t s;
        if (!getU8(data, off, s)) return false;
        f.takerSide = static_cast<Side>(s);
        fills.push_back(f);
    }
    uint8_t r;
    if (!getU8(data, off, r)) return false;
    rested = (r != 0);
    if (!getI64(data, off, remainingQty)) return false;
    return true;
}

std::vector<uint8_t> encodeSimpleResult(bool ok) {
    std::vector<uint8_t> buf(1);
    buf[0] = ok ? 1 : 0;
    return buf;
}

bool decodeSimpleResult(const std::vector<uint8_t>& data, bool& ok) {
    if (data.size() < 1) return false;
    ok = (data[0] != 0);
    return true;
}

} // namespace order_protocol
} // namespace app
