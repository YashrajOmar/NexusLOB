#include "Config.h"

namespace app {

const PeerInfo* AppConfig::self() const {
    for (const auto& p : peers) {
        if (p.id == selfId) return &p;
    }
    return nullptr;
}

std::map<raft::NodeId, PeerAddr>
AppConfig::peerAddrMap() const {
    std::map<raft::NodeId, PeerAddr> m;
    for (const auto& p : peers) {
        m[p.id] = {p.host, p.raftPort};
    }
    return m;
}

AppConfig::RaftBuildConfig AppConfig::toRaftConfig() const {
    RaftBuildConfig rc;
    rc.id               = selfId;
    rc.electionTick     = electionTick;
    rc.heartbeatTick    = heartbeatTick;
    rc.maxSizePerMsg    = maxSizePerMsg;
    rc.maxInflightMsgs  = maxInflightMsgs;
    for (const auto& p : peers) {
        rc.peerIds.push_back(p.id);
    }
    return rc;
}

} // namespace app
