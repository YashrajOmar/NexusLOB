#pragma once

#include "raft/Types.h"

#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <atomic>
#include <thread>

namespace app {

// ClientRequest: a parsed client command.
struct ClientRequest {
    enum Op { SET, GET, DEL };
    Op          op;
    std::string key;
    std::string value;   // only for SET
};

// ClientResponse: the result of executing a ClientRequest.
struct ClientResponse {
    bool        ok = true;       // false on error (e.g., not leader)
    std::string err;            // error message if !ok
    bool        found = false;   // for GET/DEL: did the key exist?
    std::string value;          // for GET: the value; for SET/DEL: the old value
    raft::NodeId leaderHint = 0; // if not leader, who is (0 = unknown)
};

// ProposalCallback: invoked when a proposed command is committed and applied.
// The Server registers this; ClientServer uses it to wait for results.
using ProposalCallback = std::function<void(const ClientResponse&)>;

// ClientServer: accepts client connections, parses commands, routes
// writes through raft and serves reads directly (or via ReadIndex later).
//
// Client wire protocol (text-based, newline-delimited):
//   SET <key> <value>\n
//   GET <key>\n
//   DEL <key>\n
//
// Response format:
//   OK [value]\n           — success, optional value
//   ERR <message>\n        — error
//   NOTLEADER <id>\n       — not leader, redirect to <id> (0 = unknown)
class ClientServer {
public:
    ClientServer(uint16_t port);
    ~ClientServer();

    // Register the proposal handler. ClientServer calls this to submit
    // a command to raft. The handler should block until the command is
    // committed and applied, then return the response.
    void setProposalHandler(std::function<ClientResponse(const ClientRequest&)> handler);

    // Start the background accept loop.
    void start();

    // Stop the server.
    void stop();

private:
    void acceptLoop();
    void handleClient(int sock);

    // Parse a text command line into a ClientRequest.
    static bool parseCommand(const std::string& line, ClientRequest& req);

    // Format a ClientResponse into a text reply.
    static std::string formatResponse(const ClientResponse& resp);

    // Write all bytes to a socket (blocking, looped).
    static bool writeAll(int sock, const std::string& s);

    uint16_t port_;
    std::atomic<bool> running_{false};
    std::thread acceptThread_;

    std::function<ClientResponse(const ClientRequest&)> proposalHandler_;
};

} // namespace app
