#pragma once

#include "raft/Types.h"

#include <string>
#include <vector>
#include <map>
#include <cstdint>

namespace app {

// PeerAddr: network location of a raft peer.
struct PeerAddr {
    std::string host;
    uint16_t    port;
};

// PeerInfo: identity + address of one node in the cluster.
struct PeerInfo {
    raft::NodeId id;
    std::string host;
    uint16_t    raftPort;     // port for raft RPCs (RaftTransport)
    uint16_t    clientPort;   // port for client commands (ClientServer)
};

// AppConfig: everything main.cpp needs to spin up a node.
struct AppConfig {
    // This node's identity.
    raft::NodeId selfId;
    std::string  dataDir;       // where log.bin / hardstate.bin / snapshot.bin live

    // Peer roster (including self).
    std::vector<PeerInfo> peers;

    // Raft timing (in abstract ticks — the app drives the tick cadence).
    uint32_t electionTick  = 10;
    uint32_t heartbeatTick = 1;

    // I/O bounds.
    uint64_t maxSizePerMsg   = 1ull << 20;  // 1 MiB
    uint32_t maxInflightMsgs = 256;

    // Look up this node's own PeerInfo from the peers list.
    const PeerInfo* self() const;

    // Build the NodeId → PeerAddr map for RaftTransport.
    std::map<raft::NodeId, PeerAddr> peerAddrMap() const;

    // Build the raft::Config for RawNode construction.
    struct RaftBuildConfig {
        raft::NodeId   id;
        uint32_t       electionTick;
        uint32_t       heartbeatTick;
        uint64_t       maxSizePerMsg;
        uint32_t       maxInflightMsgs;
        std::vector<raft::NodeId> peerIds;
    };
    RaftBuildConfig toRaftConfig() const;
};

} // namespace app
