#pragma once

#include "protocol/CommandCodec.h"

namespace app {

// KVCodec: encode/decode for the key-value state machine (Phase 1).
class KVCodec : public CommandCodec {
public:
    std::vector<uint8_t> encode(const ClientRequest& req) override;
    ClientResponse decodeResult(const std::vector<uint8_t>& result) override;
};

} // namespace app
