# NexusLOB — Raft-Replicated Limit Order Book

![CI](https://github.com/YashrajOmar/NexusLOB/actions/workflows/ci.yml/badge.svg)
![License](https://img.shields.io/badge/license-MIT-blue.svg)
![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)

A fault-tolerant, replicated limit order book built on a from-scratch Raft consensus engine in C++17. Three nodes agree on every order, survive crashes, and recover automatically — the matching engine state is replicated across the cluster.

Built without any Raft library. The consensus algorithm, write-ahead log, network transport, matching engine, and binary order protocol are all implemented from the ground up.

The project follows etcd/raft's library-first architecture: a pure algorithm layer (`raft/`) with zero I/O dependencies, wired to a concrete disk + TCP application layer (`app/`).

---

## Phase 1 — Consensus Engine

Raft consensus library + replicated KV store. Three nodes that agree on every write, survive crashes, and recover automatically.

### What's built

- **Leader election** — randomized timeout, Figure 4 states
- **Log replication** — AppendEntries, prevLogTerm check, conflict truncation
- **Commit rule** — Figure 8: current-term entries only
- **Snapshot/InstallSnapshot** — Section 7
- **Crash recovery** — fsync'd WAL (3 files: log.bin, hardstate.bin, confstate.bin)
- **Follower redirect** — NOTLEADER response with leader hint
- **Zero-I/O invariant** — `raft/` has zero `#include` for sockets or files; communicates via a `Ready` struct. Verified by grep.

### Tests (9)

| Test | What it checks |
|---|---|
| `test_interaction` | Data-driven determinism test |
| `test_election` | 3-node cluster picks exactly one leader |
| `test_log_replication` | Leader's entries reach all followers |
| `test_kv_apply` | FSM applies SET/GET/DEL correctly |
| `test_wal_crash_recovery` | WAL survives a simulated crash + restart |
| `test_property` | 200 random chaos trials, safety invariants hold |
| `test_stress` | 10k rapid proposals, all replicate |
| `test_partition` | Network split + heal, all nodes converge |
| `test_benchmark` | KV latency p50/p99 baseline |

### Results

![Phase 1 Demo](docs/phase1_demo.svg)

```
KV Replication Benchmark (in-process, no network/disk)
  p50 (median):  33.5 us
  p90:           35.8 us
  p99:           48.2 us
  p99.9:         98.8 us
  Throughput:    29,203 ops/sec
```

---

## Phase 2 — LOB Integration

Limit order book matching engine connected to the Raft consensus engine. Orders are replicated across 3 fault-tolerant nodes with deterministic matching.

### What's built

- **OrderBook** — L3 price-time priority matching engine (FIFO at each price level), L3 internal book with L2 depth view, cancel-replace semantics, multi-level sweeps. Fixed-point prices for deterministic matching across replicas.
- **LOBStateMachine** — implements the `StateMachine` interface, wraps `OrderBook`, decodes binary order commands in `apply()`, serializes book in `snapshot()`/`restore()`. Deterministic sequence counter (same on all replicas).
- **Binary wire protocol** — length-prefixed frames (`[len:4][opcode:1][payload]`) for NEW/CXL/MOD/BOOK. Replaces text protocol.
- **Three read modes** — Direct (fast, local), LeaseBased (leader lease), ReadIndex (quorum confirmation, linearizable). Selectable via `--readmode` flag.
- **Dependency injection** — Server has zero mode checks. FSM + codec + book reader all injected at construction. Adding a new FSM type doesn't change Server.
- **ReadIndex wired into raft** — leader broadcasts heartbeat with token, waits for quorum acks, emits `ReadState`. Was stubbed in Phase 1, now connected end-to-end.

### Tests (4 new, 13 total)

| Test | What it checks |
|---|---|
| `test_lob_apply` | Matching engine: 14 tests — price-time priority, sweeps, cancel, modify, L2 depth, duplicate rejection |
| `test_order_protocol` | Binary protocol encode/decode + FSM apply + snapshot/restore + cross-replica determinism |
| `test_order_replication` | Orders replicated across 3 nodes — basic, matching, cancel, partition heal, 50-order chaos |
| `test_lob_benchmark` | Replicated order latency p50/p99 + throughput |

### Results

![Phase 2 Demo](docs/phase2_demo.svg)
![Phase 2 Verification](docs/phase2_verify.svg)

```
LOB Matching Engine (in-memory, 2000-order book)
  Latency:       0.4 us per order
  Algorithm:     Price-time priority (FIFO at each price level)
  Determinism:   Verified (identical results across 3 replicas)

Replicated Order (3-node cluster, in-process)
  p50 (median):  641 us
  p90:           725 us
  p99:           983 us
  p99.9:         1500 us
  Throughput:    1,567 orders/sec
```

All benchmarks run in-process (no network, no disk) on a local dev machine. Production latency adds `fsync` (~1ms) and network RTT (~0.5ms). Numbers vary slightly run-to-run due to OS scheduling.

---

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

**The key invariant:** `raft/` contains zero `#include` directives for files, sockets, or any I/O library. The algorithm never touches the outside world — it outputs a `Ready` struct describing what needs persisting/sending/applying, and the application layer does the actual I/O. Verified by grep.

**Dependency injection:** Server receives its FSM, codec, and book reader via constructor injection — zero mode checks. Adding a new FSM type means writing new classes and injecting them; Server never changes.

---

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

---

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

## Demo Scripts

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\phase1_demo.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\phase2_demo.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\verify.ps1
```

---

## Phase 3 (Next)

- **Skip list matching engine** — replace `std::map` with a lock-free skip list for O(log n) concurrent matching
- **Group commit** — batch fsync across multiple entries (10x latency win)
- **Pipelined AppendEntries** — don't wait for ack before sending the next batch
- **Membership changes** — `ConfChange` types are defined, the protocol isn't implemented

## References

- [The Raft paper](https://raft.github.io/raft.pdf) — Ongaro & Ousterhout
- [etcd/raft](https://github.com/etcd-io/raft) — the Go implementation whose architecture was followed
- [etcd raftexample](https://github.com/etcd-io/etcd/tree/release-3.7/contrib/raftexample) — the Ready-loop pattern

## License

MIT
