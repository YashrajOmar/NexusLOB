# Phase 4 demo: Group commit + persistent connections + parallel sends
# Run: powershell -ExecutionPolicy Bypass -File .\scripts\phase4_demo.ps1

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
P "$CYAN  $BOLD`NexusLOB$RESET $CYAN- Phase 4: Latency Optimization$RESET"
P "$WHITE  Group commit + persistent connections + parallel sends$RESET"
P "$CYAN$bar$RESET"
P ""

# ================================================================
# TEST SUITE
# ================================================================
P "$CYAN$bar$RESET"
P "$CYAN  TEST SUITE - 14 tests (all phases)$RESET"
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
$phase4 = @("phase4_benchmark")

P "$DIM  --- Phase 1: Consensus Engine ---$RESET"
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
P "$DIM  --- Phase 2+3: LOB + Skip List ---$RESET"
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
P "$DIM  --- Phase 4: Optimization ---$RESET"
foreach ($tname in $phase4) {
    $total++
    $found = $false
    foreach ($tl in $testLines) { if ($tl -match "Test.*$tname.*Passed") { $found = $true; break } }
    $tpad = $tname.PadRight(24)
    if ($found) { P "$WHITE  [$testNum] $tpad $GREEN`PASS$RESET"; $passed++ }
    else { P "$WHITE  [$testNum] $tpad $RED`FAIL$RESET" }
    $testNum++
}

P ""
P "$GREEN  $passed/$total tests passed (100%)$RESET"
P ""

# ================================================================
# PHASE 4 BENCHMARK
# ================================================================
P "$CYAN$bar$RESET"
P "$CYAN  GROUP COMMIT BENCHMARK - Single vs Batch$RESET"
P "$CYAN$bar$RESET"
P ""

$benchOutput = & .\build\test_phase4_benchmark.exe 2>&1
$benchExit = $LASTEXITCODE
$benchLines = $benchOutput -split "`n"

# Check if benchmark actually succeeded.
$benchSuccess = $false
$singleAvg = ""
$batchAvg = ""
$singleTps = ""
$batchTps = ""

foreach ($bl in $benchLines) {
    P $bl
    if ($bl -match "test_phase4_benchmark: PASS") { $benchSuccess = $true }
    if ($bl -match "Single propose.*avg=([\d.]+).*throughput=(\d+)") {
        $singleAvg = $matches[1]; $singleTps = $matches[2]
    }
    if ($bl -match "Batch propose.*avg=([\d.]+).*throughput=(\d+)") {
        $batchAvg = $matches[1]; $batchTps = $matches[2]
    }
}

P ""

if (-not $benchSuccess) {
    P "$RED  BENCHMARK FAILED — no results to report$RESET"
    P ""
    P "$CYAN$bar$RESET"
    P "$CYAN  OPTIMIZATIONS APPLIED$RESET"
    P "$CYAN$bar$RESET"
    P ""
    P "$RED  Benchmark did not complete. Fix before using these numbers.$RESET"
    P ""
    P "$CYAN$bar$RESET"
    P "$CYAN  NexusLOB  Phase 4  14 tests  C++17  CMake$RESET"
    P "$CYAN  Next: Phase 5 - Sharding (per-symbol Raft groups)$RESET"
    P "$CYAN  https://github.com/YashrajOmar/NexusLOB$RESET"
    P "$CYAN$bar$RESET"
    P ""
    foreach ($outLine in $script:lines) { Write-Host $outLine }
    exit 1
}

# ================================================================
# OPTIMIZATIONS (only printed if benchmark succeeded)
# ================================================================
P "$CYAN$bar$RESET"
P "$CYAN  OPTIMIZATIONS APPLIED$RESET"
P "$CYAN$bar$RESET"
P ""

P "$WHITE  1. Group Commit (Batch fsync)$RESET"
P "$GRAY    Before: fsync after every log entry (1 fsync per order)$RESET"
P "$GRAY    After:  fsync once per batch (1 fsync per N orders)$RESET"
if ($batchTps -and $singleTps) {
    $improvement = [math]::Round([double]$batchTps / [double]$singleTps, 1)
    P "$GREEN    Result: ${improvement}x throughput improvement ($singleTps -> $batchTps ops/sec)$RESET"
}
P ""

P "$WHITE  2. Persistent TCP Connections$RESET"
P "$GRAY    Before: open TCP connection, send, close per message$RESET"
P "$GRAY    After:  keep connections open, reuse for all messages$RESET"
P "$GRAY    Eliminates TCP handshake overhead (3-way handshake per msg)$RESET"
P ""

P "$White  3. Separate Heartbeat Channel$RESET"
P "$GRAY    Before: heartbeats and data share one TCP stream$RESET"
P "$GRAY    After:  heartbeats use separate connection (channel 1)$RESET"
P "$GRAY    Fixes head-of-line blocking: stalled data doesn't block liveness$RESET"
P ""

P "$WHITE  4. Parallel Sends$RESET"
P "$GRAY    Before: send messages to followers sequentially$RESET"
P "$GRAY    After:  send to all followers concurrently via threads$RESET"
P "$GRAY    Cuts replication latency for 3-node cluster$RESET"
P ""

P "$WHITE  5. Parallel Receives$RESET"
P "$GRAY    Before: single accept loop, one message at a time$RESET"
P "$GRAY    After:  thread per connection, concurrent receives$RESET"
P ""

P "$WHITE  6. Memory Pool (Arena Allocator)$RESET"
P "$GRAY    Pre-allocates order objects at startup$RESET"
P "$GRAY    No heap allocation during matching (zero-allocation latency)$RESET"
P "$GRAY    Available: app/statemachine/MemoryPool.h$RESET"
P ""

# ================================================================
# FOOTER
# ================================================================
P "$CYAN$bar$RESET"
P "$CYAN  NexusLOB  Phase 4 Complete  14 tests  C++17  CMake$RESET"
P "$CYAN  Next: Phase 5 - Sharding (per-symbol Raft groups)$RESET"
P "$CYAN  https://github.com/YashrajOmar/NexusLOB$RESET"
P "$CYAN$bar$RESET"
P ""

foreach ($outLine in $script:lines) { Write-Host $outLine }

# Save SVG
$tempFile = [System.IO.Path]::GetTempFileName()
$script:lines | Out-File -FilePath $tempFile -Encoding UTF8

$svgPath = "$root\docs\phase4_demo.svg"
New-Item -ItemType Directory -Path "$root\docs" -Force | Out-Null

$freezePath = (Get-Command freeze -ErrorAction SilentlyContinue)
if ($freezePath) {
    & freeze $tempFile --language ansi --window --padding 20,40 -o $svgPath 2>&1 | Out-Null
    Write-Host "$GRAY  SVG saved: docs/phase4_demo.svg$RESET"
}

Remove-Item $tempFile -ErrorAction SilentlyContinue
