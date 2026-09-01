#pragma once

#include "Types.h"
#include <vector>
#include <cstdint>

namespace raft {

// MessageType: all raft RPCs and internal signals.
// Matches etcd/raft's raftpb.MessageType.
enum class MessageType : uint8_t {
    MsgPropose,        // proposal from client to leader
    MsgApp,            // AppendEntries RPC (Figure 3, Section 5.3)
    MsgAppResp,        // AppendEntries response
    MsgVote,           // RequestVote RPC (Figure 3, Section 5.2)
    MsgVoteResp,       // RequestVote response
    MsgPreVote,        // PreVote RPC (thesis Section 9.6)
    MsgPreVoteResp,    // PreVote response
    MsgSnap,           // InstallSnapshot RPC (Section 7)
    MsgHeartbeat,      // heartbeat: empty AppendEntries
    MsgHeartbeatResp,  // heartbeat response
    MsgUnreachable,    // internal: peer unreachable signal
    MsgSnapStatus,     // internal: snapshot send status
    MsgCheckQuorum,    // internal: check quorum trigger
    MsgTransferLeader, // leadership transfer request
    MsgTimeoutNow,     // leader tells target to start election
    MsgReadIndex,      // read-index request for linearizable reads
    MsgReadIndexResp,  // read-index response
};

// Snapshot: FSM state plus metadata (Section 7).
struct Snapshot {
    Term  term = 0;   // term when snapshot was taken
    Index index = 0;  // last included index (log is truncated up to here)
    std::vector<uint8_t> data;
};

// Message: unified struct for all raft RPCs and internal signals.
// Fields are populated based on MessageType; unused fields stay default.
struct Message {
    MessageType type;
    NodeId to   = 0;
    NodeId from = 0;
    Term   term = 0;

    // Log fields (used across MsgApp, MsgAppResp, MsgVote, MsgSnap, etc.)
    Term   logTerm    = 0;  // term of the entry at 'index'
    Index  index      = 0;  // log index
    Index  commit     = 0;  // leader's commitIndex (in MsgApp / heartbeat)
    bool   reject     = false;
    Index  rejectHint = 0;  // hinted nextIndex on rejection

    std::vector<Entry> entries;  // MsgApp payload
    Snapshot snapshot;           // MsgSnap payload

    std::vector<uint8_t> context;  // opaque (read-index token, etc.)
};

} // namespace raft
