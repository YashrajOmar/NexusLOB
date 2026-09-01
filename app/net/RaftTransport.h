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

namespace app {

// RaftTransport: sends raft::Message to peers over TCP and delivers
// received messages via a callback.
//
// Threading model:
//   - send() is called from the raft loop thread (Server).
//   - Received messages are delivered via onRecv on a background thread.
//   - The caller (Server) is responsible for locking around RawNode.
//
// Wire format for a Message:
//   [type:1][to:8][from:8][term:8][logTerm:8][index:8][commit:8]
//   [reject:1][rejectHint:8][entriesCount:4]
//   for each entry: [term:8][index:8][type:1][dataLen:4][data]
//   [snapTerm:8][snapIndex:8][snapDataLen:4][snapData]
//   [ctxLen:4][ctx]
class RaftTransport {
public:
    RaftTransport(raft::NodeId selfId, uint16_t listenPort);
    ~RaftTransport();

    // Set the peer address map (NodeId → host:port).
    void setPeers(const std::map<raft::NodeId, PeerAddr>& peers);

    // Register the callback invoked when a message arrives from a peer.
    // Called on the transport's background thread.
    void setRecvCallback(std::function<void(const raft::Message&)> cb);

    // Send a message to m.to. Returns false if peer unknown or send fails.
    bool send(const raft::Message& m);

    // Start the background receive loop. Returns immediately.
    void start();

    // Stop the background receive loop and close sockets.
    void stop();

private:
    void recvLoop();

    raft::NodeId                              selfId_;
    uint16_t                                  listenPort_;
    std::map<raft::NodeId, PeerAddr>          peers_;
    std::function<void(const raft::Message&)> onRecv_;

    std::atomic<bool>  running_{false};
    std::mutex         peersMutex_;
};

} // namespace app
