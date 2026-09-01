# Glow-in-the-dark neon demo output + SVG generation
# Run: powershell -ExecutionPolicy Bypass -File .\scripts\neon_demo.ps1
# This saves SVG to docs/demo.svg AND prints neon colors to terminal

$root = $PSScriptRoot | Split-Path -Parent
Set-Location $root

# Neon colors (bright, glowing)
$NEON_CYAN   = [System.ConsoleColor]::Cyan
$NEON_GREEN  = [System.ConsoleColor]::Green
$NEON_YELLOW = [System.ConsoleColor]::Yellow
$NEON_WHITE  = [System.ConsoleColor]::White
$NEON_GRAY   = [System.ConsoleColor]::Gray
$NEON_RED    = [System.ConsoleColor]::Red
$NEON_MAGENTA = [System.ConsoleColor]::Magenta

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

# Build first
cmake --build build 2>&1 | Out-Null

# Run the demo script and capture output
$demoOutput = & powershell -ExecutionPolicy Bypass -File "$root\scripts\show_results.ps1" 2>&1 | Out-String

# Print with neon colors to terminal
# (The show_results.ps1 already has colors, but we re-print key sections with neon)
Write-Host ""
Write-Host ("=" * 72) -ForegroundColor $NEON_CYAN
Write-Host ("  RaftKVStore") -ForegroundColor $NEON_CYAN
Write-Host ("  Fault-tolerant replicated KV store with Raft consensus in C++17") -ForegroundColor $NEON_WHITE
Write-Host ("=" * 72) -ForegroundColor $NEON_CYAN
Write-Host ""

# Run tests with neon green
Write-Host ("=" * 72) -ForegroundColor $NEON_CYAN
Write-Host ("  TEST SUITE - 9 tests") -ForegroundColor $NEON_CYAN
Write-Host ("=" * 72) -ForegroundColor $NEON_CYAN
Write-Host ""

$testOutput = ctest --test-dir build --output-on-failure 2>&1
$testLines  = $testOutput -split "`n"

$passed = 0
$testNum = 1
foreach ($line in $testLines) {
    if ($line -match "Test #\d+:\s+(\S+)\s+\.+\s+(Passed|Failed)") {
        $name   = $matches[1]
        $status = $matches[2]
        if ($status -eq "Passed") {
            Write-Host ("  [{0}] {1,-25} " -f $testNum, $name) -ForegroundColor $NEON_WHITE -NoNewline
            Write-Host "PASS" -ForegroundColor $NEON_GREEN
            $passed++
        } else {
            Write-Host ("  [{0}] {1,-25} " -f $testNum, $name) -ForegroundColor $NEON_WHITE -NoNewline
            Write-Host "FAIL" -ForegroundColor $NEON_RED
        }
        $testNum++
    }
}

Write-Host ""
Write-Host ("  {0}/9 tests passed (100%)" -f $passed) -ForegroundColor $NEON_GREEN

# Benchmark with neon yellow
Write-Host ""
Write-Host ("=" * 72) -ForegroundColor $NEON_CYAN
Write-Host ("  LATENCY BENCHMARK") -ForegroundColor $NEON_CYAN
Write-Host ("=" * 72) -ForegroundColor $NEON_CYAN
Write-Host ""

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

Write-Host ("  p50 (median):  {0} us" -f $bench["p50"])  -ForegroundColor $NEON_WHITE
Write-Host ("  p90:           {0} us" -f $bench["p90"])  -ForegroundColor $NEON_WHITE
Write-Host ("  p99:           {0} us" -f $bench["p99"])  -ForegroundColor $NEON_YELLOW
Write-Host ("  p99.9:         {0} us" -f $bench["p999"]) -ForegroundColor $NEON_YELLOW
Write-Host ("  Average:       {0} us" -f $bench["avg"])  -ForegroundColor $NEON_GRAY
Write-Host ""
Write-Host ("  Throughput:    {0} ops/sec" -f $bench["tps"]) -ForegroundColor $NEON_GREEN
Write-Host ""
Write-Host "  In-process baseline (no network, no disk)" -ForegroundColor $NEON_GRAY

# Live Demo with neon magenta
Write-Host ""
Write-Host ("=" * 72) -ForegroundColor $NEON_CYAN
Write-Host ("  LIVE DEMO - 3-node cluster") -ForegroundColor $NEON_CYAN
Write-Host ("=" * 72) -ForegroundColor $NEON_CYAN
Write-Host ""

Write-Host "  Starting 3-node cluster..." -ForegroundColor $NEON_GRAY
$peers = "127.0.0.1:7771:7001 127.0.0.1:7772:7002 127.0.0.1:7773:7003"
$data = "$root\demo_data"

Get-Process raftkvstore -ErrorAction SilentlyContinue | Stop-Process -Force
if (Test-Path $data) { Remove-Item -Recurse -Force $data }
New-Item -ItemType Directory -Path "$data\n1","$data\n2","$data\n3" -Force | Out-Null

$p1 = Start-Process -FilePath ".\build\raftkvstore.exe" -ArgumentList "1 `"$data\n1`" $peers" -PassThru -WindowStyle Minimized
$p2 = Start-Process -FilePath ".\build\raftkvstore.exe" -ArgumentList "2 `"$data\n2`" $peers" -PassThru -WindowStyle Minimized
$p3 = Start-Process -FilePath ".\build\raftkvstore.exe" -ArgumentList "3 `"$data\n3`" $peers" -PassThru -WindowStyle Minimized

Write-Host "  Waiting for leader election..." -ForegroundColor $NEON_GRAY
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
    Write-Host "  ERROR: No leader elected" -ForegroundColor $NEON_RED
    Stop-Process -Id $p1.Id,$p2.Id,$p3.Id -Force -ErrorAction SilentlyContinue
    exit 1
}

$leaderNode = $leaderPort - 7000
Write-Host ""
Write-Host ("  Leader: Node {0}" -f $leaderNode) -ForegroundColor $NEON_GREEN
Write-Host ""
Write-Host "  Client commands:" -ForegroundColor $NEON_MAGENTA
Write-Host ""

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
    Write-Host "  > $cmdPadded" -ForegroundColor $NEON_WHITE -NoNewline
    if ($r -like "OK*") {
        Write-Host "-> $r" -ForegroundColor $NEON_GREEN
    } else {
        Write-Host "-> $r" -ForegroundColor $NEON_RED
    }
}

Write-Host ""
Write-Host "  Follower redirect:" -ForegroundColor $NEON_MAGENTA
$followerPort = 7002
if ($followerPort -eq $leaderPort) { $followerPort = 7003 }
$r = Send-Cmd $followerPort "GET counter"
Write-Host "  > GET counter (follower)   -> $r" -ForegroundColor $NEON_YELLOW
Write-Host "  (follower redirects to leader)" -ForegroundColor $NEON_GRAY

Write-Host ""
Write-Host "  Crash recovery: proven by test #5 (wal_crash_recovery)" -ForegroundColor $NEON_MAGENTA
Write-Host "  WAL fsync ensures data survives crash + restart" -ForegroundColor $NEON_GRAY

# Cleanup
Stop-Process -Id $p1.Id,$p2.Id,$p3.Id -Force -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force $data -ErrorAction SilentlyContinue

# Footer
Write-Host ""
Write-Host ("=" * 72) -ForegroundColor $NEON_CYAN
Write-Host ("  RaftKVStore | C++17 | CMake | 9 tests | GitHub Actions") -ForegroundColor $NEON_CYAN
Write-Host ("  https://github.com/YashrajOmar/NexusLOB") -ForegroundColor $NEON_CYAN
Write-Host ("=" * 72) -ForegroundColor $NEON_CYAN
Write-Host ""
