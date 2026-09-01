#pragma once

#include "statemachine/StateMachine.h"

#include <string>
#include <unordered_map>
#include <mutex>

namespace app {

// KVStateMachine: a key-value store implementing the StateMachine interface.
//
// Command wire format (the bytes passed to apply()):
//   [opcode:1][keyLen:4][key bytes][valLen:4][val bytes (SET only)]
//
// Opcodes:
//   0x01 SET   — set key=value, return previous value (or empty if new)
//   0x02 GET   — return value for key (or empty if missing)
//   0x03 DEL   — delete key, return previous value (or empty if missing)
//
// Thread safety: internal mutex. The raft algorithm calls apply() serially,
// but clients may call get() concurrently for direct (non-replicated) reads.
class KVStateMachine : public StateMachine {
public:
    KVStateMachine();

    // --- StateMachine interface ---

    std::vector<uint8_t> apply(const std::vector<uint8_t>& data) override;
    std::vector<uint8_t> snapshot() const override;
    void restore(const std::vector<uint8_t>& data) override;

    // --- Direct read (bypasses the log; not linearizable by itself) ---
    // Used by app/Server for fast local reads or as a building block for
    // ReadIndex-based linearizable reads.

    bool get(const std::string& key, std::string& out) const;
    bool exists(const std::string& key) const;
    size_t size() const;

private:
    // Encode/decode command bytes.
    static std::vector<uint8_t> encodeSet(const std::string& k, const std::string& v);
    static std::vector<uint8_t> encodeGet(const std::string& k);
    static std::vector<uint8_t> encodeDel(const std::string& k);

    // Parse a command into opcode + key + value.
    static bool decode(const std::vector<uint8_t>& data,
                       uint8_t& op,
                       std::string& key,
                       std::string& val);

    // Serialize/deserialize the whole map for snapshots.
    std::vector<uint8_t> serializeMap() const;
    void deserializeMap(const std::vector<uint8_t>& data);

    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::string> map_;
};

} // namespace app
