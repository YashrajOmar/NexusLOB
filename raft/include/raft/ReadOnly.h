#pragma once

#include "Types.h"
#include "RPC.h"

#include <vector>
#include <map>
#include <optional>
#include <cstdint>

namespace raft {

// ReadIndex option (thesis §6.4, etcd's ReadOnlyOption).
enum class ReadOnlyOption : uint8_t {
    Safe,        // ReadIndex: leader checks quorum before answering
    LeaseBased,  // leader answers from its lease without quorum check
};

// ReadState: result of a ReadIndex round, handed to the application.
// The app waits until appliedIndex >= index, then serves the read.
struct ReadState {
    Index                  index = 0;
    std::vector<uint8_t>   requestCtx;  // opaque token to match request to reply
};

// ReadOnly: tracks pending ReadIndex requests on the leader.
// Matches etcd's readOnly struct in read_only.go.
//
// Lifecycle of a linearizable read:
//   1. Follower (or leader) calls step(MsgReadIndex) with a client token.
//   2. Leader adds the request to ReadOnly, broadcasts a heartbeat with the token.
//   3. Followers reply MsgHeartbeatResp with the same token.
//   4. Leader records each ack. Once a quorum acks, leader advances ReadOnly:
//      - emits ReadState{index = commitIndex at step-2 time, requestCtx = token}
//      - replies MsgReadIndexResp{index, ...} to the original sender.
//   5. Sender waits until appliedIndex >= index, then reads the FSM.
class ReadOnly {
public:
    explicit ReadOnly(ReadOnlyOption option = ReadOnlyOption::Safe);

    // Add a pending ReadIndex request. The leader calls this when it receives
    // MsgReadIndex. 'ctx' is the opaque client token used to match the later
    // quorum acks to this request.
    void add(Index index, const std::vector<uint8_t>& ctx);

    // Record a heartbeat ack for a pending request. Returns the request's
    // read index if a quorum has now acknowledged it (so the leader can reply
    // to the original reader); otherwise returns nullopt.
    std::optional<Index> recvAck(const Message& m, std::size_t quorum);

    // Advance past the acknowledged request at 'ctx', returning its index.
    // Used after the leader has emitted the ReadState for that ctx.
    std::optional<Index> advance(const std::vector<uint8_t>& ctx);

    // Reset all pending state. Called on term/leader changes.
    void reset();

    // Change the read option (e.g., Safe <-> LeaseBased).
    void setOption(ReadOnlyOption option) { option_ = option; }
    ReadOnlyOption option() const { return option_; }

private:
    ReadOnlyOption option_;

    // Pending requests keyed by client token. Value is the index at which
    // the request was registered (the leader's commitIndex at that moment).
    std::map<std::vector<uint8_t>, Index> pending_;

    // For each pending token, the set of peer IDs that have acked.
    std::map<std::vector<uint8_t>, std::vector<NodeId>> acks_;

    // FIFO order of registration, so advances happen in registration order.
    std::vector<std::vector<uint8_t>> recvQueue_;
};

} // namespace raft
