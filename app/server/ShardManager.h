#pragma once

#include "raft/RawNode.h"
#include "storage/WriteAheadLog.h"
#include "storage/SnapshotFile.h"
#include "statemachine/LOBStateMachine.h"
#include "protocol/OrderProtocol.h"
#include "Config.h"

#include <string>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>

namespace app {

// ShardManager: manages multiple Raft groups, one per symbol.
//
// Phase 5: horizontal scaling. Instead of one Raft group handling the
// entire order book, each symbol gets its own independent Raft group
// with its own RawNode, LOBStateMachine, and WAL. Orders are routed
// to the correct shard based on the symbol in the command.
//
// This is how real exchanges scale: AAPL and GOOG are independent
// books that can be processed in parallel, on different cores or
// even different machines.
//
// The ShardManager exposes a simple submit() API:
//   auto result = shardManager.submit(symbol, cmd);
// It routes to the right shard, proposes through that shard's Raft
// group, and returns the apply() result.
class ShardManager {
public:
    ShardManager(const AppConfig& config, uint32_t electionTick = 10,
                 uint32_t heartbeatTick = 1);
    ~ShardManager();

    // Start all shards' raft loops.
    void start();

    // Stop all shards.
    void stop();

    // Submit a command to the shard for the given symbol.
    // Creates the shard if it doesn't exist yet.
    std::vector<uint8_t> submit(const std::string& symbol,
                                const std::vector<uint8_t>& cmd);

    // Get the FSM for a symbol (for reads).
    LOBStateMachine* getFSM(const std::string& symbol);

    // Get all symbols that have shards.
    std::vector<std::string> symbols() const;

    // Number of shards.
    size_t shardCount() const;

    // Is any shard's RawNode the leader?
    bool isLeader() const;

private:
    struct Pending {
        std::vector<uint8_t> result;
        bool                 done = false;
    };

    struct Shard {
        std::unique_ptr<WriteAheadLog>   wal;
        std::unique_ptr<SnapshotFile>     snapFile;
        std::unique_ptr<LOBStateMachine>  fsm;
        std::unique_ptr<raft::RawNode>    node;
        std::thread                       loopThread;
        std::atomic<bool>                 running{false};

        std::mutex                                          pendingMutex;
        std::condition_variable                             pendingCv;
        std::map<raft::Index, std::shared_ptr<Pending>>      pending;
    };

    void createShard(const std::string& symbol);
    void shardLoop(Shard* shard);
    void processReady(Shard* shard, const raft::Ready& rd);

    AppConfig        config_;
    uint32_t         electionTick_;
    uint32_t         heartbeatTick_;
    mutable std::mutex shardsMutex_;
    std::map<std::string, std::unique_ptr<Shard>> shards_;
};

} // namespace app
