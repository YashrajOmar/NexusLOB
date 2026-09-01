#pragma once

#include "raft/RawNode.h"
#include "storage/WriteAheadLog.h"
#include "storage/SnapshotFile.h"
#include "statemachine/KVStateMachine.h"
#include "net/RaftTransport.h"
#include "net/ClientServer.h"
#include "Config.h"

#include <thread>
#include <mutex>
#include <condition_variable>
#include <map>
#include <memory>
#include <atomic>

namespace app {

// Server: the running node. Owns all components and runs the Ready loop.
//
// This is the etcd "raftexample" pattern translated to C++:
//   1. Tick the RawNode at a fixed interval.
//   2. Feed received messages into the RawNode.
//   3. Poll Ready, persist entries, send messages, apply committed entries.
//   4. Advance.
//
// Threading:
//   - raftLoop() runs on its own background thread.
//   - RaftTransport's recv callback fires on the transport's thread.
//   - ClientServer's proposal handler fires on the client thread.
//   - raftMutex_ protects all RawNode access.
class Server {
public:
    explicit Server(const AppConfig& config);
    ~Server();

    void start();
    void stop();

    bool        isLeader() const;
    raft::NodeId leaderId() const;

private:
    void raftLoop();
    void onRaftMessage(const raft::Message& m);
    ClientResponse onClientRequest(const ClientRequest& req);
    void processReady(const raft::Ready& rd);
    void applyCommittedEntries(const std::vector<raft::Entry>& entries);

    // --- components (order matters for init order) ---
    AppConfig        config_;
    WriteAheadLog    wal_;
    SnapshotFile     snapFile_;
    KVStateMachine   fsm_;
    RaftTransport    transport_;
    ClientServer     clientServer_;
    raft::RawNode    node_;

    // --- threading ---
    std::thread       loopThread_;
    std::atomic<bool> running_{false};
    std::mutex        raftMutex_;

    // Incoming message queue (transport thread → raft loop).
    std::mutex                  msgMutex_;
    std::condition_variable      msgCond_;
    std::vector<raft::Message>  pendingMsgs_;

    // Pending proposals: index → waiting client.
    struct PendingProposal {
        ClientResponse  resp;
        bool            done = false;
    };
    std::mutex                                   pendingMutex_;
    std::condition_variable                      pendingCv_;
    std::map<raft::Index, std::shared_ptr<PendingProposal>> pending_;
};

} // namespace app
