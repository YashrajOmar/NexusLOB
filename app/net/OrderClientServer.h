#pragma once

#include "statemachine/IOrderBook.h"
#include "protocol/CommandCodec.h"

#include <functional>
#include <vector>
#include <cstdint>
#include <thread>
#include <atomic>

namespace app {

// Binary frame opcodes for the order client protocol.
namespace order_wire {
    constexpr uint8_t OP_NEW  = 0x10;
    constexpr uint8_t OP_CXL  = 0x11;
    constexpr uint8_t OP_MOD  = 0x12;
    constexpr uint8_t OP_BOOK = 0x04;   // read query (not a raft command)
    constexpr uint8_t OP_ERR  = 0xFF;
}

// OrderClientServer: a binary, length-prefixed TCP server for order
// commands. Replaces the text-based ClientServer for LOB mode.
//
// Wire format (all multi-byte fields little-endian):
//
//   Request:  [length:4][opcode:1][payload...]
//     NEW:  [orderId:8][side:1][price:8][quantity:8]
//     CXL:  [orderId:8]
//     MOD:  [orderId:8][newPrice:8][newQuantity:8]
//     BOOK: [maxLevels:4]   (read query — not replicated)
//
//   Response: [length:4][opcode:1][status:1][payload...]
//     NEW resp:  [fillCount:4](fills)*[rested:1][remaining:8]
//     CXL/MOD:   (empty payload, status tells ok/fail)
//     BOOK resp: [bidCount:4](bids)*[askCount:4](asks)*
//     ERR:       [msgLen:4][msg bytes]
//
// Threading: single-threaded accept loop (one client at a time, like
// ClientServer). A production server would use a thread pool or async I/O.
class OrderClientServer {
public:
    // SubmitCallback: propose bytes through raft, return apply result.
    using SubmitCallback = std::function<std::vector<uint8_t>(const std::vector<uint8_t>&)>;

    // ReadBookCallback: read the book (bids + asks) from the FSM.
    using ReadBookCallback = std::function<std::pair<std::vector<PriceLevel>, std::vector<PriceLevel>>(size_t)>;

    // IsLeaderCallback: check if this node is the leader.
    using IsLeaderCallback = std::function<bool()>;

    OrderClientServer(uint16_t port);
    ~OrderClientServer();

    void setSubmitCallback(SubmitCallback cb) { submitCb_ = std::move(cb); }
    void setReadBookCallback(ReadBookCallback cb) { readCb_ = std::move(cb); }
    void setIsLeaderCallback(IsLeaderCallback cb) { isLeaderCb_ = std::move(cb); }

    void start();
    void stop();

private:
    void acceptLoop();
    void handleClient(int sock);

    // Frame I/O: [length:4][payload]
    bool readFrame(int sock, std::vector<uint8_t>& payload);
    bool writeFrame(int sock, const std::vector<uint8_t>& payload);
    bool writeRaw(int sock, const uint8_t* data, size_t len);

    // Command handlers
    std::vector<uint8_t> handleNew(const uint8_t* p, size_t len);
    std::vector<uint8_t> handleCxl(const uint8_t* p, size_t len);
    std::vector<uint8_t> handleMod(const uint8_t* p, size_t len);
    std::vector<uint8_t> handleBook(const uint8_t* p, size_t len);
    std::vector<uint8_t> makeError(const std::string& msg);

    uint16_t port_;
    std::atomic<bool> running_{false};
    std::thread acceptThread_;

    SubmitCallback      submitCb_;
    ReadBookCallback    readCb_;
    IsLeaderCallback    isLeaderCb_;
};

} // namespace app
