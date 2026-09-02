#include "protocol/LOBCodec.h"

#include <string>

namespace app {

std::vector<uint8_t> LOBCodec::encode(const ClientRequest& req) {
    // Map the text protocol onto order commands.
    //   SET buy|sell  "price:qty" -> NEW order
    //   DEL <orderId>             -> CXL order
    //   GET                       -> no write (reads via BookReader)
    if (req.op == ClientRequest::SET) {
        Side side = (req.key == "sell") ? Side::Sell : Side::Buy;
        auto colon = req.value.find(':');
        if (colon == std::string::npos) return {};
        int64_t price = std::stoll(req.value.substr(0, colon));
        int64_t qty   = std::stoll(req.value.substr(colon + 1));
        uint64_t orderId = nextOrderId_.fetch_add(1);
        return order_protocol::encodeNew(orderId, side, price, qty);
    }
    if (req.op == ClientRequest::DEL) {
        uint64_t orderId = std::stoull(req.key);
        return order_protocol::encodeCancel(orderId);
    }
    return {};
}

ClientResponse LOBCodec::decodeResult(const std::vector<uint8_t>& result) {
    ClientResponse resp;
    if (result.empty()) {
        resp.ok = false;
        return resp;
    }

    // The first opcode byte of the command tells us which result format to
    // expect. But the apply() result doesn't carry the opcode — it's just
    // the encoded result. We try to decode as a NEW result first (most
    // common), then fall back to a simple result (CXL/MOD/reject).
    std::vector<Fill> fills;
    bool rested;
    int64_t remaining;
    if (order_protocol::decodeNewResult(result, fills, rested, remaining)) {
        resp.ok    = true;
        resp.found = !fills.empty();
        // Serialize a summary: "filled <n> @<price>, rest=<bool>"
        std::string summary;
        for (const auto& f : fills) {
            summary += "F " + std::to_string(f.quantity) + "@" +
                       std::to_string(f.price) + " ";
        }
        if (rested) summary += "REST " + std::to_string(remaining);
        resp.value = summary;
    } else {
        bool ok;
        if (order_protocol::decodeSimpleResult(result, ok)) {
            resp.ok = ok;
        } else {
            resp.ok = false;
        }
    }
    return resp;
}

} // namespace app
