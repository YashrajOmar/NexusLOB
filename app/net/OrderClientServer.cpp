#include "net/OrderClientServer.h"
#include "protocol/OrderProtocol.h"

#include <cstring>
#include <algorithm>

#if defined(_WIN32) || defined(_MSC_VER)
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
#  define CLOSE_SOCKET(s) closesocket(s)
#  define SOCK_ERR SOCKET_ERROR
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  define CLOSE_SOCKET(s) close(s)
#  define SOCK_ERR (-1)
#endif

namespace app {

namespace {

void putU32(std::vector<uint8_t>& buf, uint32_t v) {
    for (int i = 0; i < 4; ++i) buf.push_back(static_cast<uint8_t>((v >> (i*8)) & 0xFF));
}
void putU64(std::vector<uint8_t>& buf, uint64_t v) {
    for (int i = 0; i < 8; ++i) buf.push_back(static_cast<uint8_t>((v >> (i*8)) & 0xFF));
}
void putI64(std::vector<uint8_t>& buf, int64_t v) { putU64(buf, static_cast<uint64_t>(v)); }

bool getU32(const uint8_t* p, size_t len, size_t& off, uint32_t& out) {
    if (off + 4 > len) return false;
    out = 0; for (int i = 0; i < 4; ++i) out |= static_cast<uint32_t>(p[off+i]) << (i*8);
    off += 4; return true;
}
bool getU64(const uint8_t* p, size_t len, size_t& off, uint64_t& out) {
    if (off + 8 > len) return false;
    out = 0; for (int i = 0; i < 8; ++i) out |= static_cast<uint64_t>(p[off+i]) << (i*8);
    off += 8; return true;
}
bool getI64(const uint8_t* p, size_t len, size_t& off, int64_t& out) {
    uint64_t u; if (!getU64(p, len, off, u)) return false;
    out = static_cast<int64_t>(u); return true;
}

} // namespace

OrderClientServer::OrderClientServer(uint16_t port) : port_(port) {}

OrderClientServer::~OrderClientServer() { stop(); }

void OrderClientServer::start() {
    running_ = true;
    acceptThread_ = std::thread([this] { acceptLoop(); });
}

void OrderClientServer::stop() {
    running_ = false;
    if (acceptThread_.joinable()) acceptThread_.join();
}

// ============================================================
// Accept loop
// ============================================================

void OrderClientServer::acceptLoop() {
    int listenSock = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
    if (listenSock < 0) return;

    int opt = 1;
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in sin{};
    sin.sin_family = AF_INET;
    sin.sin_port   = htons(port_);
    sin.sin_addr.s_addr = htonl(INADDR_ANY);

    if (::bind(listenSock, reinterpret_cast<sockaddr*>(&sin), sizeof(sin)) == SOCK_ERR) {
        CLOSE_SOCKET(listenSock); return;
    }
    if (::listen(listenSock, 8) == SOCK_ERR) {
        CLOSE_SOCKET(listenSock); return;
    }

    while (running_) {
        int client = static_cast<int>(::accept(listenSock, nullptr, nullptr));
        if (client < 0) continue;
        handleClient(client);
        CLOSE_SOCKET(client);
    }
    CLOSE_SOCKET(listenSock);
}

// ============================================================
// Handle one client: read binary frames, dispatch, reply
// ============================================================

void OrderClientServer::handleClient(int sock) {
    while (running_) {
        std::vector<uint8_t> frame;
        if (!readFrame(sock, frame)) break;
        if (frame.empty()) break;

        uint8_t opcode = frame[0];
        const uint8_t* payload = frame.data() + 1;
        size_t payloadLen = frame.size() - 1;

        std::vector<uint8_t> response;
        switch (opcode) {
            case order_wire::OP_NEW:
                response = handleNew(payload, payloadLen);
                break;
            case order_wire::OP_CXL:
                response = handleCxl(payload, payloadLen);
                break;
            case order_wire::OP_MOD:
                response = handleMod(payload, payloadLen);
                break;
            case order_wire::OP_BOOK:
                response = handleBook(payload, payloadLen);
                break;
            default:
                response = makeError("unknown opcode");
        }
        if (!writeFrame(sock, response)) break;
    }
}

// ============================================================
// Frame I/O: [length:4][payload]
// ============================================================

bool OrderClientServer::readFrame(int sock, std::vector<uint8_t>& payload) {
    uint8_t lenBuf[4];
    size_t received = 0;
    while (received < 4) {
        ssize_t n = recv(sock, reinterpret_cast<char*>(lenBuf + received),
                        4 - received, 0);
        if (n <= 0) return false;
        received += static_cast<size_t>(n);
    }
    uint32_t length = 0;
    for (int i = 0; i < 4; ++i) length |= static_cast<uint32_t>(lenBuf[i]) << (i*8);
    if (length == 0 || length > (1 << 20)) return false;

    payload.resize(length);
    received = 0;
    while (received < length) {
        ssize_t n = recv(sock, reinterpret_cast<char*>(payload.data() + received),
                        length - received, 0);
        if (n <= 0) return false;
        received += static_cast<size_t>(n);
    }
    return true;
}

bool OrderClientServer::writeFrame(int sock, const std::vector<uint8_t>& payload) {
    uint8_t lenBuf[4];
    uint32_t length = static_cast<uint32_t>(payload.size());
    for (int i = 0; i < 4; ++i) lenBuf[i] = static_cast<uint8_t>((length >> (i*8)) & 0xFF);
    if (!writeRaw(sock, lenBuf, 4)) return false;
    if (!writeRaw(sock, payload.data(), payload.size())) return false;
    return true;
}

bool OrderClientServer::writeRaw(int sock, const uint8_t* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t w = send(sock, reinterpret_cast<const char*>(data + sent),
                        static_cast<int>(len - sent), 0);
        if (w == SOCK_ERR) return false;
        sent += static_cast<size_t>(w);
    }
    return true;
}

// ============================================================
// Command handlers
// ============================================================

std::vector<uint8_t> OrderClientServer::handleNew(const uint8_t* p, size_t len) {
    if (!isLeaderCb_ || !isLeaderCb_()) return makeError("not leader");

    size_t off = 0;
    uint64_t orderId; uint8_t side; int64_t price, qty;
    if (!getU64(p, len, off, orderId)) return makeError("bad frame");
    if (off + 1 > len) return makeError("bad frame");
    side = p[off++]; if (side > 1) return makeError("bad side");
    if (!getI64(p, len, off, price)) return makeError("bad frame");
    if (!getI64(p, len, off, qty)) return makeError("bad frame");

    auto cmd = order_protocol::encodeNew(orderId, static_cast<Side>(side), price, qty);
    if (!submitCb_) return makeError("no submit handler");
    auto result = submitCb_(cmd);

    // Build response: [OP_NEW][status:1][result bytes]
    std::vector<uint8_t> resp;
    resp.push_back(order_wire::OP_NEW);
    resp.push_back(result.empty() ? 0 : 1);  // status
    resp.insert(resp.end(), result.begin(), result.end());
    return resp;
}

std::vector<uint8_t> OrderClientServer::handleCxl(const uint8_t* p, size_t len) {
    if (!isLeaderCb_ || !isLeaderCb_()) return makeError("not leader");

    size_t off = 0;
    uint64_t orderId;
    if (!getU64(p, len, off, orderId)) return makeError("bad frame");

    auto cmd = order_protocol::encodeCancel(orderId);
    if (!submitCb_) return makeError("no submit handler");
    auto result = submitCb_(cmd);

    std::vector<uint8_t> resp;
    resp.push_back(order_wire::OP_CXL);
    resp.push_back(result.empty() ? 0 : 1);
    resp.insert(resp.end(), result.begin(), result.end());
    return resp;
}

std::vector<uint8_t> OrderClientServer::handleMod(const uint8_t* p, size_t len) {
    if (!isLeaderCb_ || !isLeaderCb_()) return makeError("not leader");

    size_t off = 0;
    uint64_t orderId; int64_t newPrice, newQty;
    if (!getU64(p, len, off, orderId)) return makeError("bad frame");
    if (!getI64(p, len, off, newPrice)) return makeError("bad frame");
    if (!getI64(p, len, off, newQty)) return makeError("bad frame");

    auto cmd = order_protocol::encodeModify(orderId, newPrice, newQty);
    if (!submitCb_) return makeError("no submit handler");
    auto result = submitCb_(cmd);

    std::vector<uint8_t> resp;
    resp.push_back(order_wire::OP_MOD);
    resp.push_back(result.empty() ? 0 : 1);
    resp.insert(resp.end(), result.begin(), result.end());
    return resp;
}

std::vector<uint8_t> OrderClientServer::handleBook(const uint8_t* p, size_t len) {
    if (!readCb_) return makeError("no book reader");

    size_t off = 0;
    uint32_t maxLevels = 0;
    getU32(p, len, off, maxLevels);  // 0 = all levels

    auto [bids, asks] = readCb_(maxLevels);

    // Response: [OP_BOOK][status:1][bidCount:4](bids)*[askCount:4](asks)*
    std::vector<uint8_t> resp;
    resp.push_back(order_wire::OP_BOOK);
    resp.push_back(1);  // status = ok
    putU32(resp, static_cast<uint32_t>(bids.size()));
    for (const auto& pl : bids) {
        putI64(resp, pl.price);
        putI64(resp, pl.totalQuantity);
        putU32(resp, pl.orderCount);
    }
    putU32(resp, static_cast<uint32_t>(asks.size()));
    for (const auto& pl : asks) {
        putI64(resp, pl.price);
        putI64(resp, pl.totalQuantity);
        putU32(resp, pl.orderCount);
    }
    return resp;
}

std::vector<uint8_t> OrderClientServer::makeError(const std::string& msg) {
    std::vector<uint8_t> resp;
    resp.push_back(order_wire::OP_ERR);
    resp.push_back(0);  // status = fail
    putU32(resp, static_cast<uint32_t>(msg.size()));
    resp.insert(resp.end(), msg.begin(), msg.end());
    return resp;
}

} // namespace app
