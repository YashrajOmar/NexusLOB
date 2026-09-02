#include "server/Server.h"
#include "server/ReadMode.h"
#include "statemachine/KVStateMachine.h"
#include "statemachine/LOBStateMachine.h"
#include "protocol/KVCodec.h"
#include "protocol/LOBCodec.h"
#include "net/OrderClientServer.h"
#include "Config.h"

#include <iostream>
#include <csignal>
#include <thread>
#include <chrono>
#include <string>

namespace {
app::Server* g_server = nullptr;

void signalHandler(int) {
    if (g_server) g_server->stop();
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0]
                  << " <id> <dataDir> <peer1Host:raftPort:clientPort> [peer2...] "
                     "[--mode kv|lob] [--readmode direct|readindex|lease] "
                     "[--orderport N]\n"
                  << "Example: " << argv[0]
                  << " 1 ./data/n1 127.0.0.1:9999:8000 127.0.0.1:9998:8001 127.0.0.1:9997:8002 --mode lob --readmode readindex --orderport 9000\n";
        return 1;
    }

    app::AppConfig cfg;
    cfg.selfId = static_cast<raft::NodeId>(std::stoull(argv[1]));
    cfg.dataDir = argv[2];

    std::string modeStr = "kv";
    app::ReadMode readMode = app::ReadMode::Direct;
    uint16_t orderPort = 0;  // 0 = don't start binary order server

    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--mode" && i + 1 < argc) {
            modeStr = argv[++i];
            continue;
        }
        if (arg == "--readmode" && i + 1 < argc) {
            std::string rm = argv[++i];
            if (rm == "readindex") readMode = app::ReadMode::ReadIndex;
            else if (rm == "lease") readMode = app::ReadMode::LeaseBased;
            else readMode = app::ReadMode::Direct;
            continue;
        }
        if (arg == "--orderport" && i + 1 < argc) {
            orderPort = static_cast<uint16_t>(std::stoul(argv[++i]));
            continue;
        }
        if (arg == "--electionTick" && i + 1 < argc) {
            cfg.electionTick = static_cast<uint32_t>(std::stoul(argv[++i]));
            continue;
        }
        auto p1 = arg.find(':');
        auto p2 = arg.find(':', p1 + 1);
        if (p1 == std::string::npos || p2 == std::string::npos) {
            std::cerr << "Bad peer: " << arg << "\n";
            return 1;
        }
        app::PeerInfo peer;
        peer.host = arg.substr(0, p1);
        peer.raftPort = static_cast<uint16_t>(std::stoul(arg.substr(p1 + 1, p2 - p1 - 1)));
        peer.clientPort = static_cast<uint16_t>(std::stoul(arg.substr(p2 + 1)));
        peer.id = static_cast<raft::NodeId>(cfg.peers.size() + 1);
        cfg.peers.push_back(peer);
    }

    bool found = false;
    for (const auto& p : cfg.peers) {
        if (p.id == cfg.selfId) { found = true; break; }
    }
    if (!found) {
        std::cerr << "selfId " << cfg.selfId << " not in peer list\n";
        return 1;
    }

    // --- Dependency injection: build components for the mode ---
    std::unique_ptr<app::StateMachine> fsm;
    std::unique_ptr<app::CommandCodec> codec;
    app::BookReader* bookReader = nullptr;
    std::unique_ptr<app::OrderClientServer> orderServer;

    if (modeStr == "lob") {
        auto lob = std::make_unique<app::LOBStateMachine>("LOB");
        bookReader = lob.get();
        fsm = std::move(lob);
        codec = std::make_unique<app::LOBCodec>();
        if (orderPort > 0) {
            orderServer = std::make_unique<app::OrderClientServer>(orderPort);
        }
        std::cerr << "Starting in LOB mode (readMode=" << static_cast<int>(readMode)
                  << ", orderPort=" << orderPort << ")\n";
    } else {
        fsm = std::make_unique<app::KVStateMachine>();
        codec = std::make_unique<app::KVCodec>();
        std::cerr << "Starting in KV mode\n";
    }

    try {
        std::cerr << "Node " << cfg.selfId << " starting (dataDir=" << cfg.dataDir << ")\n";
        app::Server server(cfg, std::move(fsm), std::move(codec), bookReader,
                           std::move(orderServer), readMode);
        g_server = &server;
        std::signal(SIGINT, signalHandler);
        server.start();
        std::cerr << "Node " << cfg.selfId << " started\n";

        while (g_server) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            static raft::Role lastRole = raft::Role::Follower;
            raft::Role r = server.isLeader() ? raft::Role::Leader : raft::Role::Follower;
            if (r != lastRole) {
                std::cerr << "Node " << cfg.selfId << " is now "
                          << (r == raft::Role::Leader ? "LEADER" : "FOLLOWER") << "\n";
                lastRole = r;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
