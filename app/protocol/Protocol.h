#pragma once

#include <string>
#include <cstdint>

namespace app {
namespace protocol {

// Opcodes matching KVStateMachine's binary format, used when encoding
// a proposed command into the bytes that raft replicates.
constexpr uint8_t OP_SET = 0x01;
constexpr uint8_t OP_GET = 0x02;
constexpr uint8_t OP_DEL = 0x03;

// Text protocol commands (what clients type).
constexpr const char* CMD_SET = "SET";
constexpr const char* CMD_GET = "GET";
constexpr const char* CMD_DEL = "DEL";

// Encode a client command into the binary format that KVStateMachine::apply
// expects. Used by Server when proposing a command into raft.
std::string encodeSet(const std::string& key, const std::string& value);
std::string encodeGet(const std::string& key);
std::string encodeDel(const std::string& key);

// Parse the apply() result bytes returned by KVStateMachine back into
// a ClientResponse-friendly form.
//   [found:1][valLen:4][val]
bool decodeApplyResult(const std::string& bytes,
                       bool& found,
                       std::string& value);

} // namespace protocol
} // namespace app
