# crash_test.ps1: black-box crash recovery test.
# Starts 3 nodes, writes data, kills one mid-flight, restarts it,
# verifies the restarted node caught up.

$ErrorActionPreference = "Stop"
$root      = Resolve-Path "$PSScriptRoot/.."
$buildDir  = "$root/build"
$exe       = "$buildDir/raftkvstore.exe"
$dataRoot  = "$root/test_data_crash"

if (-not (Test-Path $exe)) {
    Write-Error "Build first: cmake -B build; cmake --build build"
    exit 1
}

# Clean slate.
if (Test-Path $dataRoot) { Remove-Item -Recurse -Force $dataRoot }
New-Item -ItemType Directory -Path $dataRoot | Out-Null

$nodes = @(
    @{ id = 1; raftPort = 9991; clientPort = 8001 },
    @{ id = 2; raftPort = 9992; clientPort = 8002 },
    @{ id = 3; raftPort = 9993; clientPort = 8003 }
)

$procs = @()

function Start-Node($n) {
    $dataDir = "$dataRoot/n$($n.id)"
    New-Item -ItemType Directory -Path $dataDir -Force | Out-Null
    $peerArgs = "127.0.0.1:9991:8001 127.0.0.1:9992:8002 127.0.0.1:9993:8003"
    $p = Start-Process -FilePath $exe `
        -ArgumentList "$($n.id) $dataDir $peerArgs" `
        -PassThru -NoNewWindow -RedirectStandardOutput "$dataDir/stdout.log"
    return $p
}

function Send-Cmd($port, $cmd) {
    $client = New-Object System.Net.Sockets.TcpClient
    $client.Connect("127.0.0.1", $port)
    $stream = $client.GetStream()
    $writer = New-Object System.IO.StreamWriter($stream)
    $reader = New-Object System.IO.StreamReader($stream)
    $writer.WriteLine($cmd)
    $writer.Flush()
    $resp = $reader.ReadLine()
    $client.Close()
    return $resp
}

# --- 1. Start all 3 nodes ---
Write-Host "Starting 3 nodes..."
foreach ($n in $nodes) {
    $procs += Start-Node $n
}
Start-Sleep -Seconds 3

# Find the leader (try SET on each, the leader returns OK).
$leaderPort = 0
foreach ($n in $nodes) {
    $resp = Send-Cmd $n.clientPort "SET probe 1"
    if ($resp -like "OK*") {
        $leaderPort = $n.clientPort
        Write-Host "Leader found: node $($n.id) (port $leaderPort)"
        break
    }
}
if ($leaderPort -eq 0) {
    Write-Error "No leader elected"
    exit 1
}

# --- 2. Write 100 entries ---
Write-Host "Writing 100 entries..."
for ($i = 1; $i -le 100; $i++) {
    $resp = Send-Cmd $leaderPort "SET key$i value$i"
    if ($resp -notlike "OK*") {
        Write-Warning "Write $i returned: $resp"
    }
}
Write-Host "Wrote 100 entries."

# --- 3. Kill node 2 mid-flight ---
$victim = $nodes[1]
Write-Host "Killing node $($victim.id) (simulating crash)..."
Stop-Process -Id $procs[1].Id -Force
$procs[1] = $null

# Keep writing to the remaining 2 nodes.
Write-Host "Writing 50 more entries with node 2 down..."
for ($i = 101; $i -le 150; $i++) {
    $resp = Send-Cmd $leaderPort "SET key$i value$i"
}
Start-Sleep -Seconds 1

# --- 4. Restart node 2 ---
Write-Host "Restarting node $($victim.id)..."
$procs[1] = Start-Node $victim
Start-Sleep -Seconds 3

# --- 5. Verify node 2 caught up ---
Write-Host "Verifying node $($victim.id) caught up..."
$ok = $true
for ($i = 1; $i -le 150; $i++) {
    $resp = Send-Cmd $victim.clientPort "GET key$i"
    $expected = "OK value$i"
    if ($resp -ne $expected) {
        Write-Warning "key$i: expected '$expected', got '$resp'"
        $ok = $false
        break
    }
}

# --- 6. Cleanup ---
foreach ($p in $procs) {
    if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force }
}

if ($ok) {
    Write-Host "crash_test: PASS — node 2 recovered all 150 entries"
    exit 0
} else {
    Write-Host "crash_test: FAIL — data lost after crash"
    exit 1
}
