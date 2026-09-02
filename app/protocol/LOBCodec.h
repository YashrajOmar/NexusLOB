#pragma once

#include "protocol/CommandCodec.h"
#include "protocol/OrderProtocol.h"

#include <atomic>

namespace app {

// LOBCodec: encode/decode for the limit-order-book state machine (Phase 2).
//
// Maps the text ClientRequest onto order commands:
//   "SET buy|sell"  "price:qty"  -> NEW order
//   "DEL <orderId>"              -> CXL order
//   "GET"                        -> no-op (reads go via BookReader, not writes)
class LOBCodec : public CommandCodec {
public:
    LOBCodec() = default;

    std::vector<uint8_t> encode(const ClientRequest& req) override;
    ClientResponse decodeResult(const std::vector<uint8_t>& result) override;

private:
    // Monotonic order-ID generator (client-side ids before raft assigns
    // its own index). In production this would be a UUID or exchange seq.
    std::atomic<uint64_t> nextOrderId_{1};
};

} // namespace app
