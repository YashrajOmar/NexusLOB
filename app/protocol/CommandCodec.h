#pragma once

#include "statemachine/IOrderBook.h"
#include "net/ClientServer.h"

#include <vector>
#include <cstdint>

namespace app {

// CommandCodec: abstract interface for translating between client requests
// and the binary payloads raft replicates.
//
// The Server is agnostic to the FSM type — it delegates all encoding/decoding
// to an injected codec. This lets new FSM types (KV, LOB, future) plug in
// without Server changing a single line (open/closed principle).
//
// Lifecycle of a command:
//   ClientRequest --[encode]--> bytes --[raft.propose]--> committed
//   committed.data --[fsm.apply]--> result bytes --[decodeResult]--> ClientResponse
class CommandCodec {
public:
    virtual ~CommandCodec() = default;

    // Encode a client request into the binary payload raft replicates.
    virtual std::vector<uint8_t> encode(const ClientRequest& req) = 0;

    // Decode the FSM apply() result into a client response.
    virtual ClientResponse decodeResult(const std::vector<uint8_t>& result) = 0;
};

// BookReader: abstract interface for reading the order book.
// LOB mode provides this; KV mode injects nullptr.
// Server uses it to serve read queries (BBO/depth) without knowing
// the concrete FSM type.
class BookReader {
public:
    virtual ~BookReader() = default;
    virtual std::vector<PriceLevel> bids(size_t maxLevels = 0) const = 0;
    virtual std::vector<PriceLevel> asks(size_t maxLevels = 0) const = 0;
};

} // namespace app
