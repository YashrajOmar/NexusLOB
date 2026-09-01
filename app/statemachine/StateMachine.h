#pragma once

#include "raft/Types.h"

#include <vector>
#include <optional>
#include <cstdint>

namespace app {

// StateMachine: abstract interface for the replicated state machine.
//
// The raft algorithm calls apply() for each committed log entry, in order.
// The application implements this with its own logic (e.g., a KV map).
//
// apply() returns the result of the command; for a KV SET it might return
// the old value (or nullopt if the key was new). The caller (app/Server)
// uses this to reply to the waiting client.
//
// snapshot() and restore() support log compaction (Section 7):
//   - snapshot() serializes the entire FSM state to bytes.
//   - restore() replaces the FSM state with the deserialized bytes.
class StateMachine {
public:
    virtual ~StateMachine() = default;

    // Apply a committed command to the state machine.
    // Returns the result (e.g., previous value for a SET, fetched value for
    // a GET). The interpretation of 'data' is application-defined.
    virtual std::vector<uint8_t> apply(const std::vector<uint8_t>& data) = 0;

    // Serialize the entire current state to bytes (for a snapshot).
    virtual std::vector<uint8_t> snapshot() const = 0;

    // Replace the current state with the deserialized bytes (from a snapshot).
    virtual void restore(const std::vector<uint8_t>& data) = 0;
};

} // namespace app
