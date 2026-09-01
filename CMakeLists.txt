cmake_minimum_required(VERSION 3.15)
project(RaftKVStore CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# ============================================================
# Sanitizer option: cmake -B build -DENABLE_SANITIZER=ON
# ============================================================

option(ENABLE_SANITIZER "Enable AddressSanitizer + UndefinedBehaviorSanitizer" OFF)

if(ENABLE_SANITIZER)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(WARNING "Sanitizers require GCC or Clang; ignoring ENABLE_SANITIZER")
    else()
        message(STATUS "Sanitizers enabled (ASan + UBSan)")
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

# ============================================================
# raft: the algorithm library (no I/O, no sockets)
# ============================================================

add_library(raft STATIC
    raft/src/LogUnstable.cpp
    raft/src/RaftLog.cpp
    raft/src/RawNode.cpp
    raft/src/ReadOnly.cpp
    raft/src/tracker/Progress.cpp
)

target_include_directories(raft PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/raft/include
)

target_compile_features(raft PUBLIC cxx_std_17)

# Enforce the purity rule: no I/O headers in raft/.
# If someone adds #include <fstream> to raft/, this fails the build.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(raft PRIVATE -Wall -Wextra -Werror)
elseif(MSVC)
    target_compile_options(raft PRIVATE /W4 /WX)
endif()

# ============================================================
# raftkvstore: the application (wires raft to disk + sockets + KV)
# ============================================================

add_executable(raftkvstore
    app/storage/WriteAheadLog.cpp
    app/storage/SnapshotFile.cpp
    app/statemachine/KVStateMachine.cpp
    app/net/RaftTransport.cpp
    app/net/ClientServer.cpp
    app/protocol/Protocol.cpp
    app/server/Server.cpp
    app/Config.cpp
    app/main.cpp
)

target_include_directories(raftkvstore PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/app
    ${CMAKE_CURRENT_SOURCE_DIR}/raft/include
)

target_link_libraries(raftkvstore PRIVATE raft)

# Platform-specific socket libraries.
if(WIN32)
    target_link_libraries(raftkvstore PRIVATE ws2_32)
endif()

# ============================================================
# Tests
# ============================================================

enable_testing()

# InteractionTest: data-driven raft algorithm tests
add_executable(test_interaction
    raft/tests/InteractionTest.cpp
)
target_link_libraries(test_interaction PRIVATE raft)
target_include_directories(test_interaction PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/raft/include
)
add_test(NAME interaction COMMAND test_interaction)

# test_election: 3-node leader election via ClusterHarness
add_executable(test_election
    tests/test_election.cpp
    tests/ClusterHarness.cpp
)
target_link_libraries(test_election PRIVATE raft)
target_include_directories(test_election PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_CURRENT_SOURCE_DIR}/raft/include
)
add_test(NAME election COMMAND test_election)

# test_log_replication: log replication across nodes
add_executable(test_log_replication
    tests/test_log_replication.cpp
    tests/ClusterHarness.cpp
)
target_link_libraries(test_log_replication PRIVATE raft)
target_include_directories(test_log_replication PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_CURRENT_SOURCE_DIR}/raft/include
)
add_test(NAME log_replication COMMAND test_log_replication)

# test_kv_apply: KV state machine correctness
add_executable(test_kv_apply
    tests/test_kv_apply.cpp
    app/statemachine/KVStateMachine.cpp
    app/protocol/Protocol.cpp
)
target_include_directories(test_kv_apply PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/app
    ${CMAKE_CURRENT_SOURCE_DIR}/raft/include
)
add_test(NAME kv_apply COMMAND test_kv_apply)

# test_wal_crash_recovery: WAL survives crash
add_executable(test_wal_crash_recovery
    tests/test_wal_crash_recovery.cpp
    app/storage/WriteAheadLog.cpp
)
target_include_directories(test_wal_crash_recovery PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/app
    ${CMAKE_CURRENT_SOURCE_DIR}/raft/include
)
add_test(NAME wal_crash_recovery COMMAND test_wal_crash_recovery)

# Convenience target: run all tests
add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS
        test_interaction
        test_election
        test_log_replication
        test_kv_apply
        test_wal_crash_recovery
        test_property
        test_stress
        test_partition
        test_benchmark
)

# ============================================================
# Additional tests (stress, property, partition, benchmark)
# ============================================================

# test_property: random chaos, verify safety invariants
add_executable(test_property
    tests/test_property.cpp
    tests/ClusterHarness.cpp
)
target_link_libraries(test_property PRIVATE raft)
target_include_directories(test_property PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_CURRENT_SOURCE_DIR}/raft/include
)
add_test(NAME property COMMAND test_property)

# test_stress: 10k rapid proposals, verify consistency
add_executable(test_stress
    tests/test_stress.cpp
    tests/ClusterHarness.cpp
)
target_link_libraries(test_stress PRIVATE raft)
target_include_directories(test_stress PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_CURRENT_SOURCE_DIR}/raft/include
)
add_test(NAME stress COMMAND test_stress)

# test_partition: network split + heal, verify convergence
add_executable(test_partition
    tests/test_partition.cpp
    tests/ClusterHarness.cpp
)
target_link_libraries(test_partition PRIVATE raft)
target_include_directories(test_partition PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_CURRENT_SOURCE_DIR}/raft/include
)
add_test(NAME partition COMMAND test_partition)

# test_benchmark: latency measurement (p50/p99)
add_executable(test_benchmark
    tests/test_benchmark.cpp
    tests/ClusterHarness.cpp
)
target_link_libraries(test_benchmark PRIVATE raft)
target_include_directories(test_benchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_CURRENT_SOURCE_DIR}/raft/include
)
add_test(NAME benchmark COMMAND test_benchmark)
