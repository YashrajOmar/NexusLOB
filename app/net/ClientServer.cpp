#include "net/ClientServer.h"

#include <cstring>
#include <sstream>
#include <algorithm>

#if defined(_WIN32) || defined(_MSC_VER)
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
#  define CLOSE_SOCKET(s) closesocket(s)
#  define SOCK_ERR SOCKET_ERROR
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  define CLOSE_SOCKET(s) close(s)
#  define SOCK_ERR (-1)
#endif

namespace app {

ClientServer::ClientServer(uint16_t port)
    : port_(port) {}

ClientServer::~ClientServer() {
    stop();
}

void ClientServer::setProposalHandler(
        std::function<ClientResponse(const ClientRequest&)> handler) {
    proposalHandler_ = std::move(handler);
}

void ClientServer::start() {
    running_ = true;
    acceptThread_ = std::thread([this] { acceptLoop(); });
}

void ClientServer::stop() {
    running_ = false;
    if (acceptThread_.joinable()) {
        acceptThread_.join();
    }
}

// ============================================================
// Accept loop: listen, accept, handle each client in-place.
// (Single-threaded for simplicity — one client at a time.
//  A real server would spawn a thread per client or use async I/O.)
// ============================================================

void ClientServer::acceptLoop() {
    int listenSock = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
    if (listenSock < 0) return;

    int opt = 1;
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in sin{};
    sin.sin_family = AF_INET;
    sin.sin_port   = htons(port_);
    sin.sin_addr.s_addr = htonl(INADDR_ANY);

    if (::bind(listenSock, reinterpret_cast<sockaddr*>(&sin), sizeof(sin)) == SOCK_ERR) {
        CLOSE_SOCKET(listenSock);
        return;
    }
    if (::listen(listenSock, 8) == SOCK_ERR) {
        CLOSE_SOCKET(listenSock);
        return;
    }

    while (running_) {
        int client = static_cast<int>(::accept(listenSock, nullptr, nullptr));
        if (client < 0) continue;
        handleClient(client);
        CLOSE_SOCKET(client);
    }

    CLOSE_SOCKET(listenSock);
}

// ============================================================
// Handle one client: read commands line by line until disconnect.
// ============================================================

void ClientServer::handleClient(int sock) {
    std::string buffer;
    char chunk[1024];

    while (running_) {
        ssize_t n = recv(sock, chunk, sizeof(chunk), 0);
        if (n <= 0) break;

        buffer.append(chunk, static_cast<size_t>(n));

        // Process complete lines (newline-delimited protocol).
        size_t pos;
        while ((pos = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos + 1);

            // Strip trailing \r if present (CRLF clients).
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty()) continue;

            ClientRequest req;
            if (!parseCommand(line, req)) {
                std::string resp = "ERR invalid command\n";
                writeAll(sock, resp);
                continue;
            }

            ClientResponse resp;
            if (proposalHandler_) {
                resp = proposalHandler_(req);
            } else {
                resp.ok = false;
                resp.err = "no handler";
            }

            std::string reply = formatResponse(resp);
            writeAll(sock, reply);
        }
    }
}

bool ClientServer::writeAll(int sock, const std::string& s) {
    size_t sent = 0;
    while (sent < s.size()) {
        ssize_t w = send(sock, s.data() + sent,
                         static_cast<int>(s.size() - sent), 0);
        if (w == SOCK_ERR) return false;
        sent += static_cast<size_t>(w);
    }
    return true;
}

// ============================================================
// Command parsing: "SET x 5" → {op=SET, key="x", value="5"}
// ============================================================

bool ClientServer::parseCommand(const std::string& line, ClientRequest& req) {
    std::istringstream iss(line);
    std::string cmd;
    if (!(iss >> cmd)) return false;

    // Uppercase the command for case-insensitive matching.
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);

    if (cmd == "SET") {
        req.op = ClientRequest::SET;
        if (!(iss >> req.key >> req.value)) return false;
        return true;
    } else if (cmd == "GET") {
        req.op = ClientRequest::GET;
        if (!(iss >> req.key)) return false;
        return true;
    } else if (cmd == "DEL") {
        req.op = ClientRequest::DEL;
        if (!(iss >> req.key)) return false;
        return true;
    }
    return false;
}

// ============================================================
// Response formatting
// ============================================================

std::string ClientServer::formatResponse(const ClientResponse& resp) {
    if (!resp.ok) {
        // Not leader → redirect. Otherwise → error.
        if (!resp.err.empty() && resp.err == "not leader") {
            return "NOTLEADER " + std::to_string(resp.leaderHint) + "\n";
        }
        return "ERR " + resp.err + "\n";
    }

    std::ostringstream oss;
    if (resp.found) {
        oss << "OK " << resp.value << "\n";
    } else {
        oss << "OK\n";
    }
    return oss.str();
}

} // namespace app
