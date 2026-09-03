#pragma once

#include "statemachine/IOrderBook.h"

#include <cstdint>
#include <vector>

namespace app {
namespace order_protocol {

// Opcodes for order commands replicated through raft.
//   NEW  — submit a new order (limit or market via extreme price)
//   CXL  — cancel a resting order by id
//   MOD  — cancel-replace a resting order (loses time priority)
constexpr uint8_t OP_NEW  = 0x10;
constexpr uint8_t OP_CXL  = 0x11;
constexpr uint8_t OP_MOD  = 0x12;

// --- Command encoding (client → raft payload) ---

std::vector<uint8_t> encodeNew(uint64_t orderId, Side side,
                               int64_t price, int64_t quantity);

std::vector<uint8_t> encodeCancel(uint64_t orderId);

std::vector<uint8_t> encodeModify(uint64_t orderId,
                                  int64_t newPrice, int64_t newQuantity);

// --- Command decoding (raft payload → FSM apply) ---

bool decodeNew(const std::vector<uint8_t>& data,
               uint64_t& orderId, Side& side,
               int64_t& price, int64_t& quantity);

bool decodeCancel(const std::vector<uint8_t>& data, uint64_t& orderId);

bool decodeModify(const std::vector<uint8_t>& data,
                  uint64_t& orderId, int64_t& newPrice, int64_t& newQuantity);

// --- Apply-result encoding (FSM apply → Server → client) ---

// NEW result: [ok:1][fillCount:4](fills)*[rested:1][remainingQty:8]
//   ok=0 means rejected (duplicate id, zero qty). No further bytes.
std::vector<uint8_t> encodeNewResult(const std::vector<Fill>& fills,
                                     bool rested, int64_t remainingQty);
bool decodeNewResult(const std::vector<uint8_t>& data,
                     std::vector<Fill>& fills, bool& rested,
                     int64_t& remainingQty);

// CXL / MOD result: [ok:1]
std::vector<uint8_t> encodeSimpleResult(bool ok);
bool decodeSimpleResult(const std::vector<uint8_t>& data, bool& ok);

} // namespace order_protocol
} // namespace app
