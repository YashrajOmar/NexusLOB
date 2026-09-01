#include "net/RaftTransport.h"

#include <cstring>
#include <stdexcept>
#include <thread>
#include <iostream>

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
#  include <netdb.h>
#  define CLOSE_SOCKET(s) close(s)
#  define SOCK_ERR (-1)
#endif

namespace app {

namespace {

// ============================================================
// Global socket init (Winsock needs WSAStartup)
// ============================================================

struct SocketInit {
    SocketInit() {
#if defined(_WIN32) || defined(_MSC_VER)
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    }
    ~SocketInit() {
#if defined(_WIN32) || defined(_MSC_VER)
        WSACleanup();
#endif
    }
};

SocketInit g_socketInit;

// ============================================================
// Binary serialization for Message (same pattern as WAL)
// ============================================================

void appendU8 (std::vector<char>& buf, uint8_t  v) { buf.push_back(static_cast<char>(v)); }
void appendU32(std::vector<char>& buf, uint32_t v) {
    for (int i = 0; i < 4; ++i) buf.push_back(static_cast<char>((v >> (i*8)) & 0xFF));
}
void appendU64(std::vector<char>& buf, uint64_t v) {
    for (int i = 0; i < 8; ++i) buf.push_back(static_cast<char>((v >> (i*8)) & 0xFF));
}

bool readU8 (const char* buf, size_t len, size_t& off, uint8_t&  out) {
    if (off + 1 > len) return false;
    out = static_cast<uint8_t>(buf[off]); off += 1; return true;
}
bool readU32(const char* buf, size_t len, size_t& off, uint32_t& out) {
    if (off + 4 > len) return false;
    out = 0; for (int i = 0; i < 4; ++i) out |= static_cast<uint32_t>(static_cast<uint8_t>(buf[off+i])) << (i*8);
    off += 4; return true;
}
bool readU64(const char* buf, size_t len, size_t& off, uint64_t& out) {
    if (off + 8 > len) return false;
    out = 0; for (int i = 0; i < 8; ++i) out |= static_cast<uint64_t>(static_cast<uint8_t>(buf[off+i])) << (i*8);
    off += 8; return true;
}

// Serialize a Message to bytes: [totalLen:4][ ...fields... ]
std::vector<char> serializeMessage(const raft::Message& m) {
    std::vector<char> buf;
    appendU8(buf, static_cast<uint8_t>(m.type));
    appendU64(buf, m.to);
    appendU64(buf, m.from);
    appendU64(buf, m.term);
    appendU64(buf, m.logTerm);
    appendU64(buf, m.index);
    appendU64(buf, m.commit);
    appendU8(buf, m.reject ? 1 : 0);
    appendU64(buf, m.rejectHint);

    // Entries
    appendU32(buf, static_cast<uint32_t>(m.entries.size()));
    for (const auto& e : m.entries) {
        appendU64(buf, e.term);
        appendU64(buf, e.index);
        appendU8(buf, static_cast<uint8_t>(e.type));
        appendU32(buf, static_cast<uint32_t>(e.data.size()));
        buf.insert(buf.end(), e.data.begin(), e.data.end());
    }

    // Snapshot
    appendU64(buf, m.snapshot.term);
    appendU64(buf, m.snapshot.index);
    appendU32(buf, static_cast<uint32_t>(m.snapshot.data.size()));
    buf.insert(buf.end(), m.snapshot.data.begin(), m.snapshot.data.end());

    // Context
    appendU32(buf, static_cast<uint32_t>(m.context.size()));
    buf.insert(buf.end(), m.context.begin(), m.context.end());

    // Prepend total length (frame the message).
    uint32_t totalLen = static_cast<uint32_t>(buf.size());
    std::vector<char> framed;
    appendU32(framed, totalLen);
    framed.insert(framed.end(), buf.begin(), buf.end());
    return framed;
}

bool deserializeMessage(const char* buf, size_t len, raft::Message& m) {
    size_t off = 0;
    uint8_t t;  if (!readU8(buf, len, off, t))  return false; m.type = static_cast<raft::MessageType>(t);
    if (!readU64(buf, len, off, m.to))         return false;
    if (!readU64(buf, len, off, m.from))       return false;
    if (!readU64(buf, len, off, m.term))       return false;
    if (!readU64(buf, len, off, m.logTerm))    return false;
    if (!readU64(buf, len, off, m.index))      return false;
    if (!readU64(buf, len, off, m.commit))     return false;
    uint8_t rej; if (!readU8(buf, len, off, rej)) return false; m.reject = (rej != 0);
    if (!readU64(buf, len, off, m.rejectHint)) return false;

    uint32_t nEntries; if (!readU32(buf, len, off, nEntries)) return false;
    m.entries.resize(nEntries);
    for (uint32_t i = 0; i < nEntries; ++i) {
        auto& e = m.entries[i];
        if (!readU64(buf, len, off, e.term))  return false;
        if (!readU64(buf, len, off, e.index)) return false;
        uint8_t et; if (!readU8(buf, len, off, et)) return false;
        e.type = static_cast<raft::EntryType>(et);
        uint32_t dl; if (!readU32(buf, len, off, dl)) return false;
        if (off + dl > len) return false;
        e.data.assign(reinterpret_cast<const uint8_t*>(buf + off),
                      reinterpret_cast<const uint8_t*>(buf + off + dl));
        off += dl;
    }

    if (!readU64(buf, len, off, m.snapshot.term))  return false;
    if (!readU64(buf, len, off, m.snapshot.index)) return false;
    uint32_t sl; if (!readU32(buf, len, off, sl)) return false;
    if (off + sl > len) return false;
    m.snapshot.data.assign(reinterpret_cast<const uint8_t*>(buf + off),
                            reinterpret_cast<const uint8_t*>(buf + off + sl));
    off += sl;

    uint32_t cl; if (!readU32(buf, len, off, cl)) return false;
    if (off + cl > len) return false;
    m.context.assign(reinterpret_cast<const uint8_t*>(buf + off),
                      reinterpret_cast<const uint8_t*>(buf + off + cl));
    off += cl;

    return true;
}

// Read exactly n bytes from a socket (blocking).
bool readN(int sock, char* buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        ssize_t r = recv(sock, buf + got, static_cast<int>(n - got), 0);
        if (r <= 0) return false;
        got += static_cast<size_t>(r);
    }
    return true;
}

// Write all bytes to a socket (blocking).
bool writeAll(int sock, const char* buf, size_t n) {
    size_t sent = 0;
    while (sent < n) {
        ssize_t s = send(sock, buf + sent, static_cast<int>(n - sent), 0);
        if (s == SOCK_ERR) return false;
        sent += static_cast<size_t>(s);
    }
    return true;
}

} // namespace

// ============================================================
// Construction / destruction
// ============================================================

RaftTransport::RaftTransport(raft::NodeId selfId, uint16_t listenPort)
    : selfId_(selfId)
    , listenPort_(listenPort) {}

RaftTransport::~RaftTransport() {
    stop();
}

// ============================================================
// Configuration
// ============================================================

void RaftTransport::setPeers(const std::map<raft::NodeId, PeerAddr>& peers) {
    std::lock_guard<std::mutex> lock(peersMutex_);
    peers_ = peers;
}

void RaftTransport::setRecvCallback(std::function<void(const raft::Message&)> cb) {
    onRecv_ = std::move(cb);
}

// ============================================================
// Send: open TCP connection, write framed message, close.
// ============================================================

bool RaftTransport::send(const raft::Message& m) {
    PeerAddr addr;
    {
        std::lock_guard<std::mutex> lock(peersMutex_);
        auto it = peers_.find(m.to);
        if (it == peers_.end()) return false;
        addr = it->second;
    }

    int sock = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
    if (sock < 0) return false;

    sockaddr_in sin{};
    sin.sin_family = AF_INET;
    sin.sin_port = htons(addr.port);
    inet_pton(AF_INET, addr.host.c_str(), &sin.sin_addr);

    if (::connect(sock, reinterpret_cast<sockaddr*>(&sin), sizeof(sin)) == SOCK_ERR) {
        CLOSE_SOCKET(sock);
        return false;
    }

    auto bytes = serializeMessage(m);
    bool ok = writeAll(sock, bytes.data(), bytes.size());
    CLOSE_SOCKET(sock);
    return ok;
}

// ============================================================
// Receive loop: listen, accept, read framed messages, invoke callback.
// ============================================================

void RaftTransport::start() {
    running_ = true;
    // Launch recvLoop on its own thread.
    std::thread([this] { recvLoop(); }).detach();
}

void RaftTransport::stop() {
    running_ = false;
}

void RaftTransport::recvLoop() {
    int listenSock = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
    if (listenSock < 0) {
        std::cerr << "Node " << selfId_ << ": failed to create socket\n";
        return;
    }

    int opt = 1;
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in sin{};
    sin.sin_family = AF_INET;
    sin.sin_port   = htons(listenPort_);
    sin.sin_addr.s_addr = htonl(INADDR_ANY);

    if (::bind(listenSock, reinterpret_cast<sockaddr*>(&sin), sizeof(sin)) == SOCK_ERR) {
        std::cerr << "Node " << selfId_ << ": bind failed on port " << listenPort_ << "\n";
        CLOSE_SOCKET(listenSock);
        return;
    }
    if (::listen(listenSock, 8) == SOCK_ERR) {
        std::cerr << "Node " << selfId_ << ": listen failed on port " << listenPort_ << "\n";
        CLOSE_SOCKET(listenSock);
        return;
    }
    std::cerr << "Node " << selfId_ << ": listening on port " << listenPort_ << "\n";

    while (running_) {
        int client = static_cast<int>(::accept(listenSock, nullptr, nullptr));
        if (client < 0) continue;

        // Read 4-byte length frame.
        char lenBuf[4];
        if (!readN(client, lenBuf, 4)) { CLOSE_SOCKET(client); continue; }
        uint32_t msgLen = static_cast<uint8_t>(lenBuf[0])
                       | (static_cast<uint8_t>(lenBuf[1]) << 8)
                       | (static_cast<uint8_t>(lenBuf[2]) << 16)
                       | (static_cast<uint8_t>(lenBuf[3]) << 24);

        // Read the message body.
        std::vector<char> body(msgLen);
        if (!readN(client, body.data(), msgLen)) { CLOSE_SOCKET(client); continue; }

        CLOSE_SOCKET(client);

        // Deserialize and deliver.
        raft::Message m;
        if (deserializeMessage(body.data(), body.size(), m)) {
            if (onRecv_) onRecv_(m);
        }
    }

    CLOSE_SOCKET(listenSock);
}

} // namespace app
