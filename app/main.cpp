#include "server/Server.h"
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
                  << " <id> <dataDir> <peer1Host:raftPort:clientPort> [peer2...] [--electionTick N]\n"
                  << "Example: " << argv[0]
                  << " 1 ./data/n1 127.0.0.1:9999:8000 127.0.0.1:9998:8001 127.0.0.1:9997:8002\n";
        return 1;
    }

    app::AppConfig cfg;
    cfg.selfId = static_cast<raft::NodeId>(std::stoull(argv[1]));
    cfg.dataDir = argv[2];

    // Parse peers: each arg is "host:raftPort:clientPort"
    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--electionTick" && i + 1 < argc) {
            cfg.electionTick = static_cast<uint32_t>(std::stoul(argv[++i]));
            continue;
        }
        // Parse "host:raftPort:clientPort"
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

    // Sanity: selfId must be in peers.
    bool found = false;
    for (const auto& p : cfg.peers) {
        if (p.id == cfg.selfId) { found = true; break; }
    }
    if (!found) {
        std::cerr << "selfId " << cfg.selfId << " not in peer list\n";
        return 1;
    }

    // Start the server.
    try {
        std::cerr << "Node " << cfg.selfId << " starting (dataDir=" << cfg.dataDir << ")\n";
        app::Server server(cfg);
        g_server = &server;
        std::signal(SIGINT, signalHandler);
        server.start();
        std::cerr << "Node " << cfg.selfId << " started\n";

        // Print role changes periodically.
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
