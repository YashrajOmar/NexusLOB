#include "statemachine/LOBStateMachine.h"
#include "statemachine/MapOrderBook.h"
#include "protocol/OrderProtocol.h"

#include <cstring>

namespace app {

namespace {

void putU64raw(std::vector<uint8_t>& buf, uint64_t v) {
    for (int i = 0; i < 8; ++i)
        buf.push_back(static_cast<uint8_t>((v >> (i*8)) & 0xFF));
}
bool getU64raw(const std::vector<uint8_t>& buf, size_t& off, uint64_t& out) {
    if (off + 8 > buf.size()) return false;
    out = 0;
    for (int i = 0; i < 8; ++i)
        out |= static_cast<uint64_t>(buf[off+i]) << (i*8);
    off += 8; return true;
}
void putU32raw(std::vector<uint8_t>& buf, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        buf.push_back(static_cast<uint8_t>((v >> (i*8)) & 0xFF));
}
bool getU32raw(const std::vector<uint8_t>& buf, size_t& off, uint32_t& out) {
    if (off + 4 > buf.size()) return false;
    out = 0;
    for (int i = 0; i < 4; ++i)
        out |= static_cast<uint32_t>(buf[off+i]) << (i*8);
    off += 4; return true;
}

} // namespace

// ============================================================
// Construction: inject the order book implementation
// ============================================================

LOBStateMachine::LOBStateMachine(std::unique_ptr<IOrderBook> book)
    : book_(book ? std::move(book) : std::make_unique<MapOrderBook>("LOB")) {}

// ============================================================
// StateMachine interface
// ============================================================

std::vector<uint8_t> LOBStateMachine::apply(const std::vector<uint8_t>& data) {
    if (data.empty()) return order_protocol::encodeSimpleResult(false);

    std::lock_guard<std::mutex> lock(mutex_);

    uint8_t op = data[0];
    uint64_t seq = nextSeq_++;

    switch (op) {
        case order_protocol::OP_NEW: {
            uint64_t orderId;
            Side     side;
            int64_t  price, quantity;
            if (!order_protocol::decodeNew(data, orderId, side, price, quantity)) {
                return order_protocol::encodeSimpleResult(false);
            }
            if (quantity <= 0 || book_->getOrder(orderId) != nullptr) {
                return order_protocol::encodeSimpleResult(false);
            }
            auto fills = book_->newOrder(orderId, side, price, quantity, seq);

            int64_t filled = 0;
            for (const auto& f : fills) filled += f.quantity;
            int64_t remaining = quantity - filled;
            bool rested = (remaining > 0);

            return order_protocol::encodeNewResult(fills, rested, remaining);
        }
        case order_protocol::OP_CXL: {
            uint64_t orderId;
            if (!order_protocol::decodeCancel(data, orderId)) {
                return order_protocol::encodeSimpleResult(false);
            }
            bool ok = book_->cancelOrder(orderId);
            return order_protocol::encodeSimpleResult(ok);
        }
        case order_protocol::OP_MOD: {
            uint64_t orderId;
            int64_t  newPrice, newQuantity;
            if (!order_protocol::decodeModify(data, orderId, newPrice, newQuantity)) {
                return order_protocol::encodeSimpleResult(false);
            }
            bool ok = book_->modifyOrder(orderId, newPrice, newQuantity, seq);
            return order_protocol::encodeSimpleResult(ok);
        }
        default:
            return order_protocol::encodeSimpleResult(false);
    }
}

// ============================================================
// Snapshot / restore
// ============================================================

std::vector<uint8_t> LOBStateMachine::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<uint8_t> buf;
    putU64raw(buf, nextSeq_);

    const auto& sym = book_->symbol();
    putU32raw(buf, static_cast<uint32_t>(sym.size()));
    buf.insert(buf.end(), sym.begin(), sym.end());

    auto orders = book_->allOrders();
    putU32raw(buf, static_cast<uint32_t>(orders.size()));

    for (const auto& o : orders) {
        putU64raw(buf, o.id);
        buf.push_back(static_cast<uint8_t>(o.side));
        putU64raw(buf, static_cast<uint64_t>(o.price));
        putU64raw(buf, static_cast<uint64_t>(o.quantity));
        putU64raw(buf, o.sequence);
    }
    return buf;
}

void LOBStateMachine::restore(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(mutex_);

    size_t off = 0;
    if (!getU64raw(data, off, nextSeq_)) return;

    uint32_t symLen;
    if (!getU32raw(data, off, symLen)) return;
    if (off + symLen > data.size()) return;
    std::string sym(reinterpret_cast<const char*>(data.data() + off), symLen);
    off += symLen;

    uint32_t orderCount;
    if (!getU32raw(data, off, orderCount)) return;

    // Reset the injected book with the correct symbol.
    book_->reset(sym);

    for (uint32_t i = 0; i < orderCount; ++i) {
        uint64_t orderId;
        if (!getU64raw(data, off, orderId)) return;
        uint8_t s;
        if (off + 1 > data.size()) return;
        s = data[off]; off += 1;
        if (s > 1) return;
        Side side = static_cast<Side>(s);

        uint64_t priceU, qtyU, seqU;
        if (!getU64raw(data, off, priceU)) return;
        if (!getU64raw(data, off, qtyU)) return;
        if (!getU64raw(data, off, seqU)) return;

        Order o;
        o.id       = orderId;
        o.side     = side;
        o.price    = static_cast<int64_t>(priceU);
        o.quantity = static_cast<int64_t>(qtyU);
        o.sequence = seqU;
        book_->addRestingOrder(o);
    }
}

// ============================================================
// Direct reads
// ============================================================

std::vector<PriceLevel> LOBStateMachine::bids(size_t maxLevels) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return book_->bids(maxLevels);
}

std::vector<PriceLevel> LOBStateMachine::asks(size_t maxLevels) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return book_->asks(maxLevels);
}

std::optional<int64_t> LOBStateMachine::bestBid() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return book_->bestBid();
}

std::optional<int64_t> LOBStateMachine::bestAsk() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return book_->bestAsk();
}

size_t LOBStateMachine::orderCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return book_->orderCount();
}

const std::string& LOBStateMachine::symbol() const {
    return book_->symbol();
}

} // namespace app
