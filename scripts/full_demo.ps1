# Full demo: start cluster, run commands, capture output, kill cluster
$root = $PSScriptRoot | Split-Path -Parent
$exe = "$root\build\raftkvstore.exe"
$data = "$root\demo_data"
$peers = "127.0.0.1:9991:8001 127.0.0.1:9992:8002 127.0.0.1:9993:8003"

# Clean slate
Get-Process raftkvstore -ErrorAction SilentlyContinue | Stop-Process -Force
if (Test-Path $data) { Remove-Item -Recurse -Force $data }
New-Item -ItemType Directory -Path "$data\n1","$data\n2","$data\n3" -Force | Out-Null

# Start 3 nodes in background
$p1 = Start-Process -FilePath $exe -ArgumentList "1","$data\n1",$peers -PassThru -WindowStyle Hidden
$p2 = Start-Process -FilePath $exe -ArgumentList "2","$data\n2",$peers -PassThru -WindowStyle Hidden
$p3 = Start-Process -FilePath $exe -ArgumentList "3","$data\n3",$peers -PassThru -WindowStyle Hidden

# Wait for election
Start-Sleep -Seconds 4

function Send-Cmd($port, $cmd) {
    try {
        $client = New-Object System.Net.Sockets.TcpClient
        $client.Connect("127.0.0.1", $port)
        $client.ReceiveTimeout = 10000
        $stream = $client.GetStream()
        $writer = New-Object System.IO.StreamWriter($stream)
        $reader = New-Object System.IO.StreamReader($stream)
        $writer.WriteLine($cmd)
        $writer.Flush()
        $resp = $reader.ReadLine()
        $client.Close()
        return $resp
    } catch {
        return "ERROR: $_"
    }
}

# Find leader
$leaderPort = 0
foreach ($port in 8001,8002,8003) {
    $resp = Send-Cmd $port "SET probe 1"
    if ($resp -eq "OK") {
        $leaderPort = $port
        break
    }
}

$leaderNode = $leaderPort - 8000

Write-Output "=== RaftKVStore Demo ==="
Write-Output "Leader: node $leaderNode (port $leaderPort)"
Write-Output ""

$r = Send-Cmd $leaderPort "SET user:42 alice"
Write-Output "SET user:42 alice"
Write-Output "-> $r"
Write-Output ""

$r = Send-Cmd $leaderPort "GET user:42"
Write-Output "GET user:42"
Write-Output "-> $r"
Write-Output ""

$r = Send-Cmd $leaderPort "SET counter 0"
Write-Output "SET counter 0"
Write-Output "-> $r"
Write-Output ""

$r = Send-Cmd $leaderPort "DEL user:42"
Write-Output "DEL user:42"
Write-Output "-> $r"
Write-Output ""

$r = Send-Cmd $leaderPort "GET user:42"
Write-Output "GET user:42 (after delete)"
Write-Output "-> $r"
Write-Output ""

$followerPort = 8001
if ($followerPort -eq $leaderPort) { $followerPort = 8002 }
$r = Send-Cmd $followerPort "GET counter"
Write-Output "GET counter (on follower port $followerPort)"
Write-Output "-> $r"
Write-Output ""

Write-Output "=== Demo Complete ==="

# Cleanup
Stop-Process -Id $p1.Id,$p2.Id,$p3.Id -Force -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force $data -ErrorAction SilentlyContinue
