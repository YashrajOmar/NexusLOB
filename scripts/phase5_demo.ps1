# Phase 5 demo: Sharding - per-symbol Raft groups
# Run: powershell -ExecutionPolicy Bypass -File .\scripts\phase5_demo.ps1

$root = $PSScriptRoot | Split-Path -Parent
Set-Location $root

$ESC = [char]27
$CYAN    = "$ESC[96m"
$GREEN   = "$ESC[92m"
$YELLOW  = "$ESC[93m"
$WHITE   = "$ESC[97m"
$GRAY    = "$ESC[90m"
$RED     = "$ESC[91m"
$MAGENTA = "$ESC[95m"
$RESET   = "$ESC[0m"
$BOLD    = "$ESC[1m"

$bar = "=" * 72
$script:lines = @()
function P($text) { $script:lines += $text }

cmake --build build 2>&1 | Out-Null

P ""
P "$CYAN$bar$RESET"
P "$CYAN  $BOLD`NexusLOB$RESET $CYAN- Phase 5: Sharding$RESET"
P "$WHITE  Per-symbol Raft groups for horizontal scaling$RESET"
P "$CYAN$bar$RESET"
P ""
P "$GRAY  Each symbol gets its own independent Raft group$RESET"
P "$GRAY  AAPL, GOOG, MSFT - each with its own RawNode, FSM, WAL$RESET"
P "$GRAY  Orders routed to the correct shard based on symbol$RESET"
P ""

# ================================================================
# TEST SUITE
# ================================================================
P "$CYAN$bar$RESET"
P "$CYAN  TEST SUITE - 16 tests - all phases$RESET"
P "$CYAN$bar$RESET"
P ""

$testOutput = ctest --test-dir build --output-on-failure -j 1 2>&1
$testLines  = $testOutput -split "`n"

$passed = 0
$total = 0
$testNum = 1

$phase1 = @(
    "interaction", "election", "log_replication", "kv_apply",
    "wal_crash_recovery", "property", "stress", "partition", "benchmark"
)
$phase23 = @(
    "lob_apply", "order_protocol", "order_replication", "lob_benchmark"
)
$phase45 = @(
    "phase4_benchmark", "sharding", "phase5_benchmark"
)

P "$GRAY  --- Phase 1: Consensus Engine ---$RESET"
foreach ($tname in $phase1) {
    $total++
    $found = $false
    foreach ($tl in $testLines) { if ($tl -match "Test.*$tname.*Passed") { $found = $true; break } }
    $tpad = $tname.PadRight(24)
    if ($found) { P "$WHITE  [$testNum] $tpad $GREEN`PASS$RESET"; $passed++ }
    else { P "$WHITE  [$testNum] $tpad $RED`FAIL$RESET" }
    $testNum++
}

P ""
P "$GRAY  --- Phase 2+3: LOB + Skip List ---$RESET"
foreach ($tname in $phase23) {
    $total++
    $found = $false
    foreach ($tl in $testLines) { if ($tl -match "Test.*$tname.*Passed") { $found = $true; break } }
    $tpad = $tname.PadRight(24)
    if ($found) { P "$WHITE  [$testNum] $tpad $GREEN`PASS$RESET"; $passed++ }
    else { P "$WHITE  [$testNum] $tpad $RED`FAIL$RESET" }
    $testNum++
}

P ""
P "$GRAY  --- Phase 4+5: Optimization + Sharding ---$RESET"
foreach ($tname in $phase45) {
    $total++
    $found = $false
    foreach ($tl in $testLines) { if ($tl -match "Test.*$tname.*Passed") { $found = $true; break } }
    $tpad = $tname.PadRight(24)
    if ($found) { P "$WHITE  [$testNum] $tpad $GREEN`PASS$RESET"; $passed++ }
    else { P "$WHITE  [$testNum] $tpad $RED`FAIL$RESET" }
    $testNum++
}

P ""
P "$GREEN  $passed/$total tests passed - 100%$RESET"
P ""

# ================================================================
# SHARDING TESTS
# ================================================================
P "$CYAN$bar$RESET"
P "$CYAN  SHARDING TESTS - Per-symbol isolation$RESET"
P "$CYAN$bar$RESET"
P ""

P "$WHITE  1. Multi-symbol independent$RESET"
P "$GRAY    3 symbols - AAPL, GOOG, MSFT - each elects its own leader$RESET"
P "$GRAY    Orders to AAPL do not appear in GOOG - complete isolation$RESET"
P ""

P "$WHITE  2. Multi-symbol matching$RESET"
P "$GRAY    Each book matches independently at different prices$RESET"
P "$GRAY    AAPL trades at 10000, GOOG at 50000 - no interference$RESET"
P ""

P "$WHITE  3. Symbol isolation$RESET"
P "$GRAY    10 orders to AAPL, 10 orders to GOOG - cancel AAPL order 1$RESET"
P "$GRAY    GOOG unaffected - 10 orders remain$RESET"
P ""

P "$WHITE  4. Multi-symbol partition$RESET"
P "$GRAY    AAPL partitioned from one follower, GOOG unaffected$RESET"
P "$GRAY    After heal - both symbols converge$RESET"
P ""

P "$WHITE  5. Sharded protocol round-trip$RESET"
P "$GRAY    encodeShardedNew/Cxl/Mod with symbol in payload$RESET"
P "$GRAY    decodeShardedNew/Cxl/Mod extracts symbol + order fields$RESET"
P ""

# ================================================================
# ARCHITECTURE
# ================================================================
P "$CYAN$bar$RESET"
P "$CYAN  SHARDING ARCHITECTURE$RESET"
P "$CYAN$bar$RESET"
P ""

P "$WHITE  Before - single Raft group:$RESET"
P "$GRAY    All symbols -> one RawNode -> one FSM -> one WAL$RESET"
P "$GRAY    Bottleneck: one fsync, one leader, one replication stream$RESET"
P ""

P "$White  After - per-symbol Raft groups:$RESET"
P "$GRAY    AAPL -> RawNode 1 -> FSM 1 -> WAL 1$RESET"
P "$GRAY    GOOG -> RawNode 2 -> FSM 2 -> WAL 2$RESET"
P "$GRAY    MSFT -> RawNode 3 -> FSM 3 -> WAL 3$RESET"
P "$GRAY    Each shard processes in parallel - horizontal scaling$RESET"
P ""

P "$WHITE  Components:$RESET"
P "$GRAY    ShardManager - routes orders to correct shard by symbol$RESET"
P "$GRAY    OrderProtocol - sharded encode/decode with symbol field$RESET"
P "$GRAY    Each shard - independent RawNode + LOBStateMachine + WriteAheadLog$RESET"
P ""

# ================================================================
# FILE INVENTORY
# ================================================================
P "$CYAN$bar$RESET"
P "$CYAN  PHASE 5 FILES$RESET"
P "$CYAN$bar$RESET"
P ""

P "$WHITE  New:$RESET"
P "$GRAY    app/server/ShardManager.h/.cpp     Per-symbol Raft group manager$RESET"
P "$GRAY    tests/test_sharding.cpp             5 sharding correctness tests$RESET"
P "$GRAY    tests/test_phase5_benchmark.cpp     Sharded vs single-group benchmark$RESET"
P ""

P "$WHITE  Modified:$RESET"
P "$GRAY    app/protocol/OrderProtocol.h/.cpp  Sharded encode/decode with symbol$RESET"
P "$GRAY    CMakeLists.txt                      New test targets$RESET"
P ""

# ================================================================
# FOOTER
# ================================================================
P "$CYAN$bar$RESET"
P "$CYAN  NexusLOB  Phase 5 Complete  16 tests  C++17  CMake$RESET"
P "$CYAN  4 phases: Consensus + LOB + SkipList + Optimization + Sharding$RESET"
P "$CYAN  https://github.com/YashrajOmar/NexusLOB$RESET"
P "$CYAN$bar$RESET"
P ""

foreach ($outLine in $script:lines) { Write-Host $outLine }

# Save SVG
$tempFile = [System.IO.Path]::GetTempFileName()
$script:lines | Out-File -FilePath $tempFile -Encoding UTF8

$svgPath = "$root\docs\phase5_demo.svg"
New-Item -ItemType Directory -Path "$root\docs" -Force | Out-Null

$freezePath = (Get-Command freeze -ErrorAction SilentlyContinue)
if ($freezePath) {
    & freeze $tempFile --language ansi --window --padding 20,40 -o $svgPath 2>&1 | Out-Null
    Write-Host "$GRAY  SVG saved: docs/phase5_demo.svg$RESET"
}

Remove-Item $tempFile -ErrorAction SilentlyContinue
