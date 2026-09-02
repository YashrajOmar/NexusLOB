#include "protocol/KVCodec.h"
#include "protocol/Protocol.h"

#include <string>

namespace app {

std::vector<uint8_t> KVCodec::encode(const ClientRequest& req) {
    std::string s;
    switch (req.op) {
        case ClientRequest::SET:
            s = protocol::encodeSet(req.key, req.value);
            break;
        case ClientRequest::GET:
            s = protocol::encodeGet(req.key);
            break;
        case ClientRequest::DEL:
            s = protocol::encodeDel(req.key);
            break;
    }
    return std::vector<uint8_t>(s.begin(), s.end());
}

ClientResponse KVCodec::decodeResult(const std::vector<uint8_t>& result) {
    ClientResponse resp;
    bool found = false;
    std::string value;
    protocol::decodeApplyResult(
        std::string(result.begin(), result.end()), found, value);
    resp.ok    = true;
    resp.found = found;
    resp.value = value;
    return resp;
}

} // namespace app
