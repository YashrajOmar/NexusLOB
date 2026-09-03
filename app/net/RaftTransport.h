#pragma once

#include "raft/RPC.h"
#include "raft/Types.h"
#include "../Config.h"

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <mutex>
#include <atomic>
#include <thread>

namespace app {

// RaftTransport: sends raft::Message to peers over persistent TCP connections.
//
// Phase 4 optimizations:
//   - Persistent connections (no TCP handshake per message)
//   - Separate channels for heartbeats vs data (head-of-line blocking fix)
//   - Parallel sends to all followers (concurrent, not sequential)
//   - Thread-per-connection receive (concurrent receives)
//
// Wire format for a Message:
//   [totalLen:4][type:1][to:8][from:8][term:8][logTerm:8][index:8][commit:8]
//   [reject:1][rejectHint:8][entriesCount:4]
//   for each entry: [term:8][index:8][type:1][dataLen:4][data]
//   [snapTerm:8][snapIndex:8][snapDataLen:4][snapData]
//   [ctxLen:4][ctx]
class RaftTransport {
public:
    RaftTransport(raft::NodeId selfId, uint16_t listenPort);
    ~RaftTransport();

    void setPeers(const std::map<raft::NodeId, PeerAddr>& peers);
    void setRecvCallback(std::function<void(const raft::Message&)> cb);

    // Send a message to m.to over persistent connection. Returns false on failure.
    // Heartbeats use a separate channel from data messages.
    bool send(const raft::Message& m);

    void start();
    void stop();

private:
    void acceptLoop();
    void connLoop(int sock);

    // Get or create a persistent connection to a peer.
    // channel=0 for data, channel=1 for heartbeats.
    int getOrCreateConn(raft::NodeId to, int channel);
    bool sendOnConn(int sock, const raft::Message& m);

    raft::NodeId                              selfId_;
    uint16_t                                  listenPort_;
    std::map<raft::NodeId, PeerAddr>          peers_;
    std::function<void(const raft::Message&)> onRecv_;

    std::atomic<bool>  running_{false};
    std::mutex         peersMutex_;

    // Persistent connections: (NodeId, channel) → socket.
    // channel 0 = data (AppendEntries, votes, etc.)
    // channel 1 = heartbeat (heartbeats + heartbeat responses)
    std::map<std::pair<raft::NodeId, int>, int> conns_;
    std::mutex connsMutex_;

    std::thread acceptThread_;
};

} // namespace app
