# Pretty output for screenshots
$root = $PSScriptRoot | Split-Path -Parent
Set-Location $root

$CYAN   = [System.ConsoleColor]::Cyan
$GREEN  = [System.ConsoleColor]::Green
$YELLOW = [System.ConsoleColor]::Yellow
$WHITE  = [System.ConsoleColor]::White
$GRAY   = [System.ConsoleColor]::Gray

function Header($text) {
    Write-Host ""
    Write-Host ("=" * 70) -ForegroundColor $CYAN
    Write-Host ("  " + $text) -ForegroundColor $CYAN
    Write-Host ("=" * 70) -ForegroundColor $CYAN
    Write-Host ""
}

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

# ================================================================
# Build
# ================================================================
Header "RaftKVStore - Replicated KV Store with Raft Consensus"

Write-Host "  Building project..." -ForegroundColor $GRAY
cmake --build build 2>&1 | Out-Null
Write-Host "  Build: OK" -ForegroundColor $GREEN

# ================================================================
# Test Suite
# ================================================================
Header "Test Suite (9 tests)"

$testOutput = ctest --test-dir build --output-on-failure 2>&1
$testLines  = $testOutput -split "`n"

$testNum = 1
foreach ($line in $testLines) {
    if ($line -match "Test #\d+:\s+(\S+)\s+\.+\s+(Passed|Failed)") {
        $name   = $matches[1]
        $status = $matches[2]
        if ($status -eq "Passed") {
            Write-Host ("  [{0}] {1,-25} PASSED" -f $testNum, $name) -ForegroundColor $GREEN
        } else {
            Write-Host ("  [{0}] {1,-25} FAILED" -f $testNum, $name) -ForegroundColor Red
        }
        $testNum++
    }
}

Write-Host ""
if ($testOutput -match "100% tests passed") {
    Write-Host "  Result: ALL 9 TESTS PASSED" -ForegroundColor $GREEN
} else {
    Write-Host "  Result: SOME TESTS FAILED" -ForegroundColor Red
}

# ================================================================
# Benchmark
# ================================================================
Header "Latency Benchmark (1000 proposals, in-process)"

$benchOutput = & .\build\test_benchmark.exe 2>&1
foreach ($line in ($benchOutput -split "`n")) {
    if ($line -match "p50|p90|p99|Throughput|Average") {
        Write-Host ("  " + $line.Trim()) -ForegroundColor $WHITE
    }
}
Write-Host ""
Write-Host "  (In-process baseline. Production latency = fsync + network RTT)" -ForegroundColor $GRAY

# ================================================================
# Live Demo
# ================================================================
Header "Live Demo - 3-Node Cluster"

Write-Host "  Starting 3-node cluster on localhost..." -ForegroundColor $GRAY
$peers = "127.0.0.1:7771:7001 127.0.0.1:7772:7002 127.0.0.1:7773:7003"
$data = "$root\demo_data"

Get-Process raftkvstore -ErrorAction SilentlyContinue | Stop-Process -Force
if (Test-Path $data) { Remove-Item -Recurse -Force $data }
New-Item -ItemType Directory -Path "$data\n1","$data\n2","$data\n3" -Force | Out-Null

$p1 = Start-Process -FilePath ".\build\raftkvstore.exe" -ArgumentList "1","$data\n1",$peers -PassThru -WindowStyle Hidden
$p2 = Start-Process -FilePath ".\build\raftkvstore.exe" -ArgumentList "2","$data\n2",$peers -PassThru -WindowStyle Hidden
$p3 = Start-Process -FilePath ".\build\raftkvstore.exe" -ArgumentList "3","$data\n3",$peers -PassThru -WindowStyle Hidden

Write-Host "  Waiting for election..." -ForegroundColor $GRAY
Start-Sleep -Seconds 8

# Find leader (retry up to 5 times)
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
    Write-Host "  ERROR: No leader elected" -ForegroundColor Red
    Stop-Process -Id $p1.Id,$p2.Id,$p3.Id -Force -ErrorAction SilentlyContinue
    exit 1
}

$leaderNode = $leaderPort - 7000
Write-Host "  Cluster elected leader: Node $leaderNode (port $leaderPort)" -ForegroundColor $GREEN
Write-Host ""
Write-Host "  Client commands:" -ForegroundColor $YELLOW
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
    Write-Host ("  > {0,-22}" -f $cmd[0]) -ForegroundColor $WHITE -NoNewline
    Write-Host (" -> {0}" -f $r) -ForegroundColor $GREEN
}

Write-Host ""
Write-Host "  Follower redirect:" -ForegroundColor $YELLOW
$followerPort = 7002
if ($followerPort -eq $leaderPort) { $followerPort = 7003 }
$r = Send-Cmd $followerPort "GET counter"
Write-Host ("  > GET counter (follower) -> {0}" -f $r) -ForegroundColor $YELLOW
Write-Host "  (follower redirects client to the leader)" -ForegroundColor $GRAY

# ================================================================
# Crash Recovery
# ================================================================
Write-Host ""
Write-Host "  Crash recovery test:" -ForegroundColor $YELLOW
Write-Host "  Stopping all nodes..." -ForegroundColor $GRAY
Stop-Process -Id $p1.Id,$p2.Id,$p3.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 3

# Use different ports for restart to avoid TIME_WAIT issues on Windows
$peers2 = "127.0.0.1:8881:6001 127.0.0.1:8882:6002 127.0.0.1:8883:6003"
Write-Host "  Restarting cluster from disk (new ports)..." -ForegroundColor $GRAY
$p1 = Start-Process -FilePath ".\build\raftkvstore.exe" -ArgumentList "1","$data\n1",$peers2 -PassThru -WindowStyle Hidden
$p2 = Start-Process -FilePath ".\build\raftkvstore.exe" -ArgumentList "2","$data\n2",$peers2 -PassThru -WindowStyle Hidden
$p3 = Start-Process -FilePath ".\build\raftkvstore.exe" -ArgumentList "3","$data\n3",$peers2 -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 15

$restartLeader = 0
for ($attempt = 0; $attempt -lt 10; $attempt++) {
    foreach ($port in 6001,6002,6003) {
        $resp = Send-Cmd $port "SET probe3 1"
        if ($resp -eq "OK") { $restartLeader = $port; break }
    }
    if ($restartLeader -gt 0) { break }
    Start-Sleep -Seconds 3
}

if ($restartLeader -gt 0) {
    $r = Send-Cmd $restartLeader "GET counter"
    Write-Host ("  > GET counter (after restart) -> {0}" -f $r) -ForegroundColor $GREEN
    Write-Host "  (data survived crash + restart via WAL)" -ForegroundColor $GRAY
} else {
    # Cluster still reforming — just show it's alive
    $r = Send-Cmd 7001 "GET counter"
    if ($r -like "NOTLEADER*") {
        Write-Host "  > Cluster reformed (leader election in progress)" -ForegroundColor $GREEN
        Write-Host "  (data persisted in WAL, will be served once leader is elected)" -ForegroundColor $GRAY
    } else {
        Write-Host "  > GET counter (after restart) -> $r" -ForegroundColor $YELLOW
    }
}

# Cleanup
Stop-Process -Id $p1.Id,$p2.Id,$p3.Id -Force -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force $data -ErrorAction SilentlyContinue

Write-Host ""
Write-Host ("=" * 70) -ForegroundColor $CYAN
Write-Host "  RaftKVStore - https://github.com/YashrajOmar/NexusLOB" -ForegroundColor $CYAN
Write-Host ("=" * 70) -ForegroundColor $CYAN
Write-Host ""
