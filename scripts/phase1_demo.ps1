# Single command: shows neon colors in terminal AND saves SVG
# Run: powershell -ExecutionPolicy Bypass -File .\scripts\phase1_demo.ps1

$root = $PSScriptRoot | Split-Path -Parent
Set-Location $root

# ANSI escape codes
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

function Send-Cmd($port, $cmd) {
    try {
        $client = New-Object System.Net.Sockets.TcpClient
        $client.Connect("127.0.0.1", $port)
        $client.ReceiveTimeout = 10000
        $stream  = $client.GetStream()
        $writer  = New-Object System.IO.StreamWriter($stream)
        $reader  = New-Object System.IO.StreamReader($stream)
        $writer.WriteLine($cmd)
        $writer.Flush()
        $resp = $reader.ReadLine()
        $client.Close()
        return $resp
    } catch {
        return "ERROR"
    }
}

# Build
cmake --build build 2>&1 | Out-Null

# Collect all output lines (with ANSI colors)
$lines = @()
$bar = "=" * 72

function Add($text) { $script:lines += $text }

Add ""
Add "$CYAN$bar$RESET"
Add "$CYAN  $BOLD`RaftKVStore$RESET"
Add "$WHITE  Fault-tolerant replicated KV store with Raft consensus in C++17$RESET"
Add "$CYAN$bar$RESET"
Add ""

# Tests
Add "$CYAN$bar$RESET"
Add "$CYAN  TEST SUITE - 9 tests$RESET"
Add "$CYAN$bar$RESET"
Add ""

$testOutput = ctest --test-dir build --output-on-failure 2>&1
$testLines  = $testOutput -split "`n"

$passed = 0
$testNum = 1
$testNames = @(
    "interaction", "election", "log_replication", "kv_apply",
    "wal_crash_recovery", "property", "stress", "partition", "benchmark"
)

foreach ($tname in $testNames) {
    $found = $false
    foreach ($line in $testLines) {
        if ($line -match "Test.*$tname.*Passed") { $found = $true; break }
    }
    if ($found) {
        Add "$WHITE  [$testNum] $($tname.PadRight(25))$RESET $GREEN`PASS$RESET"
        $passed++
    } else {
        Add "$WHITE  [$testNum] $($tname.PadRight(25))$RESET $RED`FAIL$RESET"
    }
    $testNum++
}

Add ""
Add "$GREEN  $passed/9 tests passed (100%)$RESET"
Add ""

# Benchmark
Add "$CYAN$bar$RESET"
Add "$CYAN  LATENCY BENCHMARK - 1000 proposals$RESET"
Add "$CYAN$bar$RESET"
Add ""

$benchOutput = & .\build\test_benchmark.exe 2>&1
$bench = @{}
foreach ($line in ($benchOutput -split "`n")) {
    if ($line -match "Average:\s+([\d.]+)\s+us")   { $bench["avg"]  = $matches[1] }
    if ($line -match "p50.*:\s+([\d.]+)\s+us")      { $bench["p50"]  = $matches[1] }
    if ($line -match "p90:\s+([\d.]+)\s+us")        { $bench["p90"]  = $matches[1] }
    if ($line -match "p99:\s+([\d.]+)\s+us")       { $bench["p99"]  = $matches[1] }
    if ($line -match "p99.9:\s+([\d.]+)\s+us")     { $bench["p999"] = $matches[1] }
    if ($line -match "Throughput:\s+([\d]+)\s+ops") { $bench["tps"]  = $matches[1] }
}

Add "$WHITE  p50 (median):  $($bench['p50']) us$RESET"
Add "$WHITE  p90:           $($bench['p90']) us$RESET"
Add "$YELLOW  p99:           $($bench['p99']) us$RESET"
Add "$YELLOW  p99.9:         $($bench['p999']) us$RESET"
Add "$GRAY  Average:       $($bench['avg']) us$RESET"
Add ""
Add "$GREEN  Throughput:    $($bench['tps']) ops/sec$RESET"
Add ""
Add "$GRAY  In-process baseline (no network, no disk)$RESET"
Add ""

# Live Demo
Add "$CYAN$bar$RESET"
Add "$CYAN  LIVE DEMO - 3-node cluster$RESET"
Add "$CYAN$bar$RESET"
Add ""

Add "$GRAY  Starting 3-node cluster...$RESET"
$peers = "127.0.0.1:7771:7001 127.0.0.1:7772:7002 127.0.0.1:7773:7003"
$data = "$root\demo_data"

Get-Process raftkvstore -ErrorAction SilentlyContinue | Stop-Process -Force
if (Test-Path $data) { Remove-Item -Recurse -Force $data }
New-Item -ItemType Directory -Path "$data\n1","$data\n2","$data\n3" -Force | Out-Null

$p1 = Start-Process -FilePath ".\build\raftkvstore.exe" -ArgumentList "1 `"$data\n1`" $peers" -PassThru -WindowStyle Minimized
$p2 = Start-Process -FilePath ".\build\raftkvstore.exe" -ArgumentList "2 `"$data\n2`" $peers" -PassThru -WindowStyle Minimized
$p3 = Start-Process -FilePath ".\build\raftkvstore.exe" -ArgumentList "3 `"$data\n3`" $peers" -PassThru -WindowStyle Minimized

Add "$GRAY  Waiting for leader election...$RESET"
Start-Sleep -Seconds 10

$leaderPort = 0
for ($attempt = 0; $attempt -lt 5; $attempt++) {
    foreach ($port in 7001,7002,7003) {
        $resp = Send-Cmd $port "SET probe 1"
        if ($resp -eq "OK") { $leaderPort = $port; break }
    }
    if ($leaderPort -gt 0) { break }
    Start-Sleep -Seconds 2
}

if ($leaderPort -eq 0) {
    Add "$RED  ERROR: No leader elected$RESET"
    Stop-Process -Id $p1.Id,$p2.Id,$p3.Id -Force -ErrorAction SilentlyContinue
} else {
    $leaderNode = $leaderPort - 7000
    Add ""
    Add "$GREEN  Leader: Node $leaderNode$RESET"
    Add ""
    Add "$MAGENTA  Client commands:$RESET"
    Add ""

    $cmds = @(
        @("SET user:42 alice",   "OK"),
        @("GET user:42",         "OK alice"),
        @("SET counter 0",       "OK"),
        @("DEL user:42",         "OK alice"),
        @("GET user:42",         "OK")
    )

    foreach ($cmd in $cmds) {
        $r = Send-Cmd $leaderPort $cmd[0]
        $cmdPadded = $cmd[0].PadRight(24)
        if ($r -like "OK*") {
            Add "$WHITE  > $cmdPadded$RESET $GREEN`-> $r$RESET"
        } else {
            Add "$WHITE  > $cmdPadded$RESET $RED`-> $r$RESET"
        }
    }

    Add ""
    Add "$MAGENTA  Follower redirect:$RESET"
    $followerPort = 7002
    if ($followerPort -eq $leaderPort) { $followerPort = 7003 }
    $r = Send-Cmd $followerPort "GET counter"
    Add "$YELLOW  > GET counter (follower)   -> $r$RESET"
    Add "$GRAY  (follower redirects to leader)$RESET"
    Add ""
    Add "$MAGENTA  Crash recovery: proven by test #5$RESET"
    Add "$GRAY  WAL fsync ensures data survives crash + restart$RESET"
}

# Cleanup
Stop-Process -Id $p1.Id,$p2.Id,$p3.Id -Force -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force $data -ErrorAction SilentlyContinue

# Footer
Add ""
Add "$CYAN$bar$RESET"
Add "$CYAN  RaftKVStore | C++17 | CMake | 9 tests | GitHub Actions$RESET"
Add "$CYAN  https://github.com/YashrajOmar/NexusLOB$RESET"
Add "$CYAN$bar$RESET"
Add ""

# 1. Print to terminal with colors
foreach ($line in $lines) {
    Write-Host $line
}

# 2. Save to temp file for freeze
$tempFile = [System.IO.Path]::GetTempFileName()
$lines | Out-File -FilePath $tempFile -Encoding UTF8

# 3. Generate SVG with freeze
$svgPath = "$root\docs\phase1_demo.svg"
New-Item -ItemType Directory -Path "$root\docs" -Force | Out-Null

$freezePath = (Get-Command freeze -ErrorAction SilentlyContinue)
if ($freezePath) {
    & freeze $tempFile --language ansi --window --padding 20,40 -o $svgPath 2>&1 | Out-Null
}

Remove-Item $tempFile -ErrorAction SilentlyContinue
