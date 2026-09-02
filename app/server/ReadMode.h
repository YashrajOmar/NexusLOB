#pragma once

#include <cstdint>

namespace app {

// ReadMode: how the server serves order-book reads (BBO/depth queries).
//
//   ReadIndex  — leader confirms quorum before reading (linearizable, safe).
//                Uses raft::ReadOnly (Safe option). One heartbeat round-trip
//                overhead per read.
//   LeaseBased — leader reads from its lease without quorum check.
//                Lower latency, risk of stale reads if the leader's lease
//                clock is wrong (e.g., partitioned leader).
//   Direct     — read the local FSM immediately. Fastest, not linearizable.
//                Stale on followers; correct on the leader right after a
//                write (read-your-own-writes).
enum class ReadMode : uint8_t {
    ReadIndex  = 0,
    LeaseBased = 1,
    Direct     = 2,
};

} // namespace app
