# NexusLOB — Raft-Replicated Limit Order Book

![CI](https://github.com/YashrajOmar/NexusLOB/actions/workflows/ci.yml/badge.svg)
![License](https://img.shields.io/badge/license-MIT-blue.svg)
![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)

> **Phase 1 (Complete):** Raft consensus library + replicated KV store  
> **Phase 2 (Complete):** LOB matching engine + binary order protocol + 3 read modes  
> **Phase 3 (Next):** Skip list matching engine for O(log n) lock-free matching

A fault-tolerant, replicated limit order book built on a from-scratch Raft consensus engine in C++17.

## What this is

A working Raft cluster: three nodes that agree on every order, survive crashes, and recover automatically. Built without any Raft library — the consensus algorithm, the write-ahead log, the network transport, the matching engine, and the binary order protocol are all implemented from the ground up.

The project follows etcd/raft's library-first architecture: a pure algorithm layer (`raft/`) with zero I/O dependencies, wired to a concrete disk + TCP application layer (`app/`). Phase 2 adds a price-time priority matching engine as the replicated state machine, with a binary order protocol and three selectable read modes (Direct / LeaseBased / ReadIndex).

## Why I built this

I wanted to understand how distributed systems achieve consistency without a single point of failure — not by reading, but by building one and watching it survive failures. Raft is the cleanest consensus algorithm to implement, and a limit order book is the most demanding state machine to put on top of it: orders must match deterministically across all replicas, with price-time priority, at sub-millisecond latency.

## What I proved I can do

- **Implement a consensus algorithm** — leader election, log replication, the commit rule (Figure 8 safety), snapshot/InstallSnapshot, all from the Raft paper
- **Separate algorithm from I/O** — the `raft/` library has zero `#include` for sockets or files; it communicates via a `Ready` struct, exactly like etcd's design. Verified by grep.
- **Build a matching engine** — price-time priority (FIFO at each price level), L3 internal book with L2 depth view, cancel-replace semantics, multi-level sweeps. Deterministic across replicas.
- **Design for extensibility** — dependency injection (Server has zero mode checks; FSM + codec + book reader all injected). Adding a new FSM type doesn't change Server.
- **Test for safety, not just correctness** — 200 random chaos trials, network partition injection, crash recovery, 10k-entry stress test, 50-order chaos with consistency verification across 3 nodes.
- **Measure performance** — in-memory matching baseline + replicated order latency benchmark as a baseline for future optimization (skip list, group commit, pipelined replication)

## Tech stack

- **C++17** — the whole project
- **CMake** — two-target build (static library + executable), sanitizer support
- **Raw POSIX/Winsock sockets** — no networking library, hand-rolled binary wire protocol
- **GitHub Actions** — CI builds + tests + sanitizers on every push

## Demo

![Phase 1 Demo](docs/phase1_demo.svg)
![Phase 2 Demo](docs/phase2_demo.svg)
![Phase 2 Verification](docs/phase2_verify.svg)

## Test Results

```
13/13 tests passed (100%)

  --- Phase 1: Consensus Engine ---
  [1] interaction              PASS
  [2] election                 PASS
  [3] log_replication          PASS
  [4] kv_apply                 PASS
  [5] wal_crash_recovery       PASS
  [6] property                 PASS
  [7] stress                   PASS
  [8] partition                PASS
  [9] benchmark                PASS

  --- Phase 2: LOB Integration ---
  [10] lob_apply               PASS
  [11] order_protocol          PASS
  [12] order_replication       PASS
  [13] lob_benchmark           PASS
```

## Benchmark

```
Phase 1 — KV Replication (in-process, no network/disk)
  p50 (median):  33.5 us
  p90:           35.8 us
  p99:           48.2 us
  p99.9:         98.8 us
  Throughput:    29,203 ops/sec

Phase 2 — LOB Matching Engine (in-memory, 2000-order book)
  Latency:       0.4 us per order
  Algorithm:     Price-time priority (FIFO at each price level)
  Determinism:   Verified (identical results across 3 replicas)

Phase 2 — Replicated Order (3-node cluster, in-process)
  p50 (median):  641 us
  p90:           725 us
  p99:           983 us
  p99.9:         1500 us
  Throughput:    1,567 orders/sec
```

All benchmarks run in-process (no network, no disk) on a local dev machine. Production latency adds `fsync` (~1ms) and network RTT (~0.5ms). Numbers vary slightly run-to-run due to OS scheduling.

## Live Demo

The SVGs above show real clusters running on localhost. Here's what happens:

**Phase 1 (KV store):**
- **3 nodes start** and automatically elect a leader via Raft consensus
- **Client commands** (SET/GET/DEL) are sent to the leader
- **Follower redirect** — if you connect to a follower, it returns `NOTLEADER <id>` so the client can redirect
- **Crash recovery** — WAL writes are fsync'd, so data survives crash + restart (proven by `test_wal_crash_recovery`)

**Phase 2 (Order book):**
- **Orders** (NEW/CXL/MOD) are sent to the leader via binary protocol
- **Matching engine** matches orders with price-time priority, all 3 nodes agree
- **Read modes** — Direct, LeaseBased, or ReadIndex (linearizable via quorum)
- **Determinism** — same log order + same sequence = identical book state on all replicas

## Build

```bash
cmake -B build && cmake --build build -j$(nproc)
```

## Run

```bash
# KV mode (Phase 1)
./build/raftkvstore 1 ./data/n1 127.0.0.1:9991:8001 127.0.0.1:9992:8002 127.0.0.1:9993:8003

# LOB mode (Phase 2) with binary protocol + ReadIndex
./build/raftkvstore 1 ./data/n1 127.0.0.1:9991:8001 127.0.0.1:9992:8002 127.0.0.1:9993:8003 --mode lob --readmode readindex --orderport 9001
```

## Tests

```bash
ctest --test-dir build --output-on-failure
```

| Test | Phase | What it checks |
|---|---|---|
| `test_election` | 1 | 3-node cluster picks exactly one leader |
| `test_log_replication` | 1 | Leader's entries reach all followers |
| `test_kv_apply` | 1 | FSM applies SET/GET/DEL correctly |
| `test_wal_crash_recovery` | 1 | WAL survives a simulated crash + restart |
| `test_property` | 1 | 200 random chaos trials, safety invariants hold |
| `test_stress` | 1 | 10k rapid proposals, all replicate |
| `test_partition` | 1 | Network split + heal, all nodes converge |
| `test_benchmark` | 1 | KV latency p50/p99 baseline |
| `test_lob_apply` | 2 | Matching engine: 14 tests, price-time priority, sweeps, cancel, modify, L2 depth |
| `test_order_protocol` | 2 | Binary protocol encode/decode + FSM + snapshot/restore + determinism |
| `test_order_replication` | 2 | Orders replicated across 3 nodes, partition heal, 50-order chaos |
| `test_lob_benchmark` | 2 | Replicated order latency p50/p99 + throughput |

## Architecture

```
raft/                          Pure algorithm — no disk, no sockets
  include/raft/               Types, RPC, Storage, RaftLog, RawNode,
                              Ready, ReadOnly (ReadIndex), Progress
  src/                        Implementations (LogUnstable, RaftLog,
                              RawNode, ReadOnly, tracker/Progress)
  tests/InteractionTest.cpp   Data-driven determinism test

app/                          Application — wires raft to real I/O
  storage/                    WriteAheadLog (disk + fsync), SnapshotFile
  statemachine/               KVStateMachine (Phase 1 FSM)
                              OrderBook (L3 matching engine, pure data structure)
                              LOBStateMachine (Phase 2 FSM, implements BookReader)
  protocol/                   Protocol (KV binary), OrderProtocol (LOB binary)
                              CommandCodec (DI interface), KVCodec, LOBCodec
  net/                        RaftTransport (TCP), ClientServer (text),
                              OrderClientServer (binary order protocol)
  server/                     Server (Ready loop, DI, 3 read modes), ReadMode
  Config.h/.cpp, main.cpp     Cluster config + entry point (--mode lob)

tests/                        ClusterHarness + 13 test files
scripts/                      Demo + crash test scripts
docs/                         Demo SVGs (Phase 1 + Phase 2)
```

**The key invariant:** `raft/` contains zero `#include` directives for files, sockets, or any I/O library. The algorithm never touches the outside world — it outputs a `Ready` struct describing what needs persisting/sending/applying, and the application layer does the actual I/O. Verified by grep. This is what makes the algorithm deterministic, testable in-process, and portable to different storage/transports.

**Dependency injection:** Server receives its FSM, codec, and book reader via constructor injection — zero mode checks. Adding a new FSM type (e.g., a futures engine) means writing new classes and injecting them; Server never changes.

## Algorithms Used

| Algorithm | Where | Purpose |
|---|---|---|
| Raft Consensus | `raft/` | Replicate order book across 3 fault-tolerant nodes |
| Price-Time Priority (FIFO) | `OrderBook.cpp` | Match orders: best price first, first-arrived first within price |
| ReadIndex (Raft §6.4) | `ReadOnly.h`, `Server.cpp` | Linearizable reads without full consensus round |
| Lease-Based Reads | `Server.cpp` | Fast reads via leader lease (no quorum check) |
| Cancel-Replace | `OrderBook.cpp` | Modify = cancel + re-add (loses time priority) |
| Fixed-Point Arithmetic | `OrderBook.h` | Deterministic matching across replicas (no float rounding) |
| Dependency Injection | `Server.h`, `CommandCodec.h` | Extensible architecture (open/closed principle) |

## What I'd improve next

- **Skip list matching engine** (Phase 3) — replace `std::map` with a lock-free skip list for O(log n) concurrent matching
- **Group commit** — batch fsync across multiple entries (10x latency win)
- **Pipelined AppendEntries** — don't wait for ack before sending the next batch
- **Membership changes** — `ConfChange` types are defined, the protocol isn't implemented

## References

- [The Raft paper](https://raft.github.io/raft.pdf) — Ongaro & Ousterhout
- [etcd/raft](https://github.com/etcd-io/raft) — the Go implementation whose architecture I followed
- [etcd raftexample](https://github.com/etcd-io/etcd/tree/release-3.7/contrib/raftexample) — the Ready-loop pattern

## License

MIT
