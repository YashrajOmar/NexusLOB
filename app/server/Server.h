#pragma once

#include "raft/RawNode.h"
#include "storage/WriteAheadLog.h"
#include "storage/SnapshotFile.h"
#include "statemachine/StateMachine.h"
#include "protocol/CommandCodec.h"
#include "net/RaftTransport.h"
#include "net/ClientServer.h"
#include "net/OrderClientServer.h"
#include "server/ReadMode.h"
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
// Pure dependency injection — Server knows nothing about the FSM type
// (KV, LOB, or future). Three interfaces are injected at construction:
//   - StateMachine:    the replicated FSM (apply/snapshot/restore)
//   - CommandCodec:    encode client requests <-> decode apply results
//   - BookReader:      optional, read queries (null for KV, LOB provides)
//   - OrderClientServer: optional, binary client server (null for KV)
//
// Read modes (Decision 4 — all three selectable):
//   - Direct:     read local FSM immediately (fastest, may be stale)
//   - LeaseBased: leader reads from lease without quorum (fast, some risk)
//   - ReadIndex:  leader confirms quorum before reading (safe, one RTT)
class Server {
public:
    Server(const AppConfig& config,
           std::unique_ptr<StateMachine> fsm,
           std::unique_ptr<CommandCodec> codec,
           BookReader* bookReader = nullptr,
           std::unique_ptr<OrderClientServer> orderServer = nullptr,
           ReadMode readMode = ReadMode::Direct);
    ~Server();

    void start();
    void stop();

    bool        isLeader() const;
    raft::NodeId leaderId() const;

    // Submit raw bytes through raft (bypasses codec). Used by binary clients.
    std::vector<uint8_t> submitRaw(const std::vector<uint8_t>& cmd);

    // Read the order book with the configured read mode (Decision 4).
    std::vector<PriceLevel> readBids(size_t maxLevels = 0);
    std::vector<PriceLevel> readAsks(size_t maxLevels = 0);

    StateMachine* fsm() { return fsm_.get(); }
    ReadMode readMode() const { return readMode_; }

private:
    void raftLoop();
    void onRaftMessage(const raft::Message& m);
    ClientResponse onClientRequest(const ClientRequest& req);
    void processReady(const raft::Ready& rd);
    void applyCommittedEntries(const std::vector<raft::Entry>& entries);
    void handleReadStates(const std::vector<raft::ReadState>& readStates);

    // --- injected components ---
    AppConfig        config_;
    ReadMode         readMode_;
    WriteAheadLog    wal_;
    SnapshotFile     snapFile_;
    std::unique_ptr<StateMachine>      fsm_;
    std::unique_ptr<CommandCodec>      codec_;
    BookReader*      bookReader_;
    std::unique_ptr<OrderClientServer> orderServer_;
    RaftTransport    transport_;
    ClientServer     clientServer_;
    raft::RawNode    node_;

    // --- threading ---
    std::thread       loopThread_;
    std::atomic<bool> running_{false};
    std::mutex        raftMutex_;

    // Incoming message queue.
    std::mutex                  msgMutex_;
    std::condition_variable      msgCond_;
    std::vector<raft::Message>  pendingMsgs_;

    // Pending proposals: index -> waiting client.
    struct PendingProposal {
        std::vector<uint8_t>    rawResult;
        bool                    done = false;
    };
    std::mutex                                   pendingMutex_;
    std::condition_variable                      pendingCv_;
    std::map<raft::Index, std::shared_ptr<PendingProposal>> pending_;

    // --- ReadIndex (linearizable reads) ---
    // Pending reads keyed by their opaque context token. When the
    // ReadState for that token arrives in a Ready, we wake the reader.
    struct PendingRead {
        size_t                  maxLevels = 0;
        bool                    isBids = true;
        std::vector<PriceLevel> result;
        bool                    done = false;
    };
    std::mutex              readMutex_;
    std::condition_variable readCv_;
    std::map<std::vector<uint8_t>, std::shared_ptr<PendingRead>> pendingReads_;
    std::atomic<uint64_t>   readTokenCounter_{1};

    // Track the applied index so ReadIndex knows when it's safe to read.
    std::atomic<raft::Index> appliedIndex_{0};
};

} // namespace app
