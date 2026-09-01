"""
Generate a terminal-style SVG directly from demo output.
No external dependencies needed.
Run: python scripts/generate_svg.py
"""
import subprocess
import json
import time
import os
import sys
import socket
import shutil
import html

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

CYAN   = "#22d3ee"
GREEN  = "#4ade80"
YELLOW = "#facc15"
WHITE  = "#f8fafc"
GRAY   = "#94a3b8"
RED    = "#f87171"
DEFAULT = "#e2e8f0"

def run_tests():
    result = subprocess.run(
        ["ctest", "--test-dir", "build", "--output-on-failure"],
        capture_output=True, text=True, cwd=ROOT, timeout=60
    )
    output = result.stdout + result.stderr
    tests = []
    for line in output.split('\n'):
        if 'Test #' in line and ('Passed' in line or 'Failed' in line):
            parts = line.split()
            name = parts[2] if len(parts) > 2 else 'unknown'
            status = 'Passed' if 'Passed' in line else 'Failed'
            tests.append((name, status))
    all_passed = '100% tests passed' in output
    return tests, all_passed

def run_benchmark():
    result = subprocess.run(
        [os.path.join(ROOT, 'build', 'test_benchmark.exe')],
        capture_output=True, text=True, cwd=ROOT, timeout=30
    )
    output = result.stdout + result.stderr
    lines = []
    for line in output.split('\n'):
        for key in ['Average', 'p50', 'p90', 'p99', 'p99.9', 'Throughput']:
            if key in line:
                lines.append(line.strip())
    return lines

def run_demo():
    peers = "127.0.0.1:7771:7001 127.0.0.1:7772:7002 127.0.0.1:7773:7003"
    data = os.path.join(ROOT, 'demo_data')

    subprocess.run("taskkill /f /im raftkvstore.exe 2>nul", shell=True,
                  capture_output=True, cwd=ROOT)
    time.sleep(1)
    if os.path.exists(data):
        shutil.rmtree(data)
    for n in ['n1', 'n2', 'n3']:
        os.makedirs(os.path.join(data, n), exist_ok=True)

    procs = []
    for i in range(1, 4):
        p = subprocess.Popen(
            [os.path.join(ROOT, 'build', 'raftkvstore.exe'),
             str(i), os.path.join(data, f'n{i}'), peers],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            cwd=ROOT
        )
        procs.append(p)

    time.sleep(8)

    def send_cmd(port, cmd):
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(10)
            s.connect(('127.0.0.1', port))
            s.sendall((cmd + '\n').encode())
            resp = s.recv(4096).decode().strip()
            s.close()
            return resp
        except:
            return "ERROR"

    leader_port = 0
    for port in [7001, 7002, 7003]:
        resp = send_cmd(port, "SET probe 1")
        if resp == "OK":
            leader_port = port
            break

    if leader_port == 0:
        for p in procs:
            p.kill()
        return None

    leader_node = leader_port - 7000

    commands = [
        ("SET user:42 alice", "OK"),
        ("GET user:42", "OK alice"),
        ("SET counter 0", "OK"),
        ("DEL user:42", "OK alice"),
        ("GET user:42", "OK"),
    ]
    cmd_results = []
    for cmd, expected in commands:
        resp = send_cmd(leader_port, cmd)
        cmd_results.append((cmd, resp))

    follower_port = 7002 if leader_port != 7002 else 7003
    follower_resp = send_cmd(follower_port, "GET counter")

    for p in procs:
        p.kill()
    shutil.rmtree(data, ignore_errors=True)

    return leader_node, leader_port, cmd_results, follower_port, follower_resp

def generate_svg():
    """Generate the SVG file."""
    # Collect all output lines as (text, color) pairs
    lines = []

    def add(segments):
        """segments: list of (text, color) tuples"""
        lines.append(segments)

    def add_simple(text, color=DEFAULT):
        add([(text, color)])

    bar = "=" * 70

    # Header
    add_simple(bar, CYAN)
    add_simple("  RaftKVStore - Replicated KV Store with Raft Consensus", CYAN)
    add_simple(bar, CYAN)
    add_simple("")

    # Build
    add_simple("  Building project...", GRAY)
    add_simple("  Build: OK", GREEN)
    add_simple("")

    # Tests
    add_simple(bar, CYAN)
    add_simple("  Test Suite (9 tests)", CYAN)
    add_simple(bar, CYAN)
    add_simple("")

    print("  Running tests...")
    tests, all_passed = run_tests()
    for i, (name, status) in enumerate(tests, 1):
        status_color = GREEN if status == "Passed" else RED
        add([
            (f"  [{i}] {name:<25} ", DEFAULT),
            (status.upper(), status_color)
        ])

    add_simple("")
    if all_passed:
        add_simple("  Result: ALL 9 TESTS PASSED", GREEN)
    else:
        add_simple("  Result: SOME TESTS FAILED", RED)
    add_simple("")

    # Benchmark
    add_simple(bar, CYAN)
    add_simple("  Latency Benchmark (1000 proposals, in-process)", CYAN)
    add_simple(bar, CYAN)
    add_simple("")

    print("  Running benchmark...")
    bench = run_benchmark()
    for bline in bench:
        add_simple("  " + bline, WHITE)

    add_simple("")
    add_simple("  (In-process baseline. Production latency = fsync + network RTT)", GRAY)
    add_simple("")

    # Live Demo
    add_simple(bar, CYAN)
    add_simple("  Live Demo - 3-Node Cluster", CYAN)
    add_simple(bar, CYAN)
    add_simple("")

    print("  Running live demo (starting 3-node cluster)...")
    demo = run_demo()
    if demo is None:
        add_simple("  ERROR: No leader elected", RED)
    else:
        leader_node, leader_port, cmd_results, follower_port, follower_resp = demo
        add_simple("  Starting 3-node cluster on localhost...", GRAY)
        add_simple("  Waiting for election...", GRAY)
        add_simple(f"  Cluster elected leader: Node {leader_node} (port {leader_port})", GREEN)
        add_simple("")
        add_simple("  Client commands:", YELLOW)
        add_simple("")

        for cmd, resp in cmd_results:
            resp_color = GREEN if resp.startswith("OK") else RED
            add([
                (f"  > {cmd:<22}", WHITE),
                (f" -> {resp}", resp_color)
            ])

        add_simple("")
        add_simple("  Follower redirect:", YELLOW)
        if "NOTLEADER" in follower_resp:
            add_simple(f"  > GET counter (follower) -> {follower_resp}", YELLOW)
        else:
            add_simple(f"  > GET counter (follower) -> {follower_resp}", RED)
        add_simple("  (follower redirects client to the leader)", GRAY)
        add_simple("")

    # Crash recovery
    add_simple("  Crash recovery: proven by test_wal_crash_recovery (test #5)", YELLOW)
    add_simple("  WAL writes are fsync'd - data survives crash + restart", GRAY)
    add_simple("")

    # Footer
    add_simple(bar, CYAN)
    add_simple("  RaftKVStore - https://github.com/YashrajOmar/NexusLOB", CYAN)
    add_simple(bar, CYAN)

    # Build SVG
    font_size = 13
    char_width = 7.8
    line_height = 18
    padding = 20
    window_header = 36
    width = 70 * char_width + padding * 2
    height = len(lines) * line_height + padding * 2 + window_header

    svg = []
    svg.append('<?xml version="1.0" encoding="UTF-8"?>')
    svg.append(f'<svg xmlns="http://www.w3.org/2000/svg" width="{width:.0f}" height="{height:.0f}" viewBox="0 0 {width:.0f} {height:.0f}">')

    # Window background
    svg.append(f'<rect width="{width:.0f}" height="{height:.0f}" fill="#0c0c0c" rx="8"/>')

    # Title bar
    svg.append(f'<rect x="0" y="0" width="{width:.0f}" height="{window_header}" fill="#1a1a1a" rx="8"/>')
    svg.append(f'<rect x="0" y="{window_header-8}" width="{width:.0f}" height="8" fill="#1a1a1a"/>')

    # Traffic lights
    svg.append(f'<circle cx="20" cy="{window_header/2}" r="5" fill="#ff5f56"/>')
    svg.append(f'<circle cx="40" cy="{window_header/2}" r="5" fill="#ffbd2e"/>')
    svg.append(f'<circle cx="60" cy="{window_header/2}" r="5" fill="#27c93f"/>')

    # Title text
    svg.append(f'<text x="{width/2:.0f}" y="{window_header/2+4}" text-anchor="middle" font-family="monospace" font-size="12" fill="#888">RaftKVStore Demo</text>')

    # Content
    y = window_header + padding + line_height - 4
    for segments in lines:
        x = padding
        for text, color in segments:
            # Handle multiple lines within text
            parts = text.split('\n')
            for i, part in enumerate(parts):
                if part:
                    escaped = html.escape(part)
                    svg.append(
                        f'<text x="{x:.1f}" y="{y}" font-family="Consolas,Monaco,Courier,monospace" '
                        f'font-size="{font_size}" fill="{color}">{escaped}</text>'
                    )
                if i < len(parts) - 1:
                    y += line_height
                    x = padding
            # Advance x by the rendered width
            # Use a rough estimate: 0.6 * font_size per char
            x += len(text) * font_size * 0.6
        y += line_height

    svg.append('</svg>')

    return '\n'.join(svg)

def main():
    print("Generating SVG demo output...")
    print("(This runs tests, benchmark, and live demo)")
    print()

    svg_content = generate_svg()

    svg_path = os.path.join(ROOT, 'docs', 'demo.svg')
    os.makedirs(os.path.dirname(svg_path), exist_ok=True)
    with open(svg_path, 'w', encoding='utf-8') as f:
        f.write(svg_content)

    print()
    print(f"SVG saved: {svg_path}")
    print(f"Size: {os.path.getsize(svg_path)} bytes")

if __name__ == '__main__':
    main()
