"""
Generate SVG from pasted terminal output.
No external dependencies, no running anything.
"""
import html
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

CYAN   = "#22d3ee"
GREEN  = "#4ade80"
YELLOW = "#facc15"
WHITE  = "#f8fafc"
GRAY   = "#94a3b8"
RED    = "#f87171"
DEFAULT = "#e2e8f0"

# The exact output to render
lines = [
    [("======================================================================", CYAN)],
    [("  RaftKVStore - Replicated KV Store with Raft Consensus", CYAN)],
    [("======================================================================", CYAN)],
    [("", DEFAULT)],
    [("  Building project...", GRAY)],
    [("  Build: OK", GREEN)],
    [("", DEFAULT)],
    [("======================================================================", CYAN)],
    [("  Test Suite (9 tests)", CYAN)],
    [("======================================================================", CYAN)],
    [("", DEFAULT)],
    [("  [1] interaction               ", DEFAULT), ("PASSED", GREEN)],
    [("  [2] election                  ", DEFAULT), ("PASSED", GREEN)],
    [("  [3] log_replication           ", DEFAULT), ("PASSED", GREEN)],
    [("  [4] kv_apply                 ", DEFAULT), ("PASSED", GREEN)],
    [("  [5] wal_crash_recovery       ", DEFAULT), ("PASSED", GREEN)],
    [("  [6] property                  ", DEFAULT), ("PASSED", GREEN)],
    [("  [7] stress                   ", DEFAULT), ("PASSED", GREEN)],
    [("  [8] partition                ", DEFAULT), ("PASSED", GREEN)],
    [("  [9] benchmark                ", DEFAULT), ("PASSED", GREEN)],
    [("", DEFAULT)],
    [("  Result: ALL 9 TESTS PASSED", GREEN)],
    [("", DEFAULT)],
    [("======================================================================", CYAN)],
    [("  Latency Benchmark (1000 proposals, in-process)", CYAN)],
    [("======================================================================", CYAN)],
    [("", DEFAULT)],
    [("  Average:     34.882 us", WHITE)],
    [("  p50 (median): 33.6 us", WHITE)],
    [("  p90:         36.3 us", WHITE)],
    [("  p99:         63.5 us", WHITE)],
    [("  p99.9:       116.2 us", WHITE)],
    [("  Throughput:  28668 ops/sec", WHITE)],
    [("", DEFAULT)],
    [("  (In-process baseline. Production latency = fsync + network RTT)", GRAY)],
    [("", DEFAULT)],
    [("======================================================================", CYAN)],
    [("  Live Demo - 3-Node Cluster", CYAN)],
    [("======================================================================", CYAN)],
    [("", DEFAULT)],
    [("  Starting 3-node cluster on localhost...", GRAY)],
    [("  Waiting for election...", GRAY)],
    [("  Cluster elected leader: Node 1 (port 7001)", GREEN)],
    [("", DEFAULT)],
    [("  Client commands:", YELLOW)],
    [("", DEFAULT)],
    [("  > SET user:42 alice      ", WHITE), ("-> OK", GREEN)],
    [("  > GET user:42            ", WHITE), ("-> OK alice", GREEN)],
    [("  > SET counter 0          ", WHITE), ("-> OK", GREEN)],
    [("  > DEL user:42            ", WHITE), ("-> OK alice", GREEN)],
    [("  > GET user:42            ", WHITE), ("-> OK", GREEN)],
    [("", DEFAULT)],
    [("  Follower redirect:", YELLOW)],
    [("  > GET counter (follower) -> NOTLEADER 1", YELLOW)],
    [("  (follower redirects client to the leader)", GRAY)],
    [("", DEFAULT)],
    [("  Crash recovery: proven by test_wal_crash_recovery (test #5)", YELLOW)],
    [("  WAL writes are fsync'd - data survives crash + restart", GRAY)],
    [("", DEFAULT)],
    [("======================================================================", CYAN)],
    [("  RaftKVStore - https://github.com/YashrajOmar/NexusLOB", CYAN)],
    [("======================================================================", CYAN)],
]

def generate_svg():
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
            if not text:
                continue
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
            x += len(text) * font_size * 0.6
        y += line_height

    svg.append('</svg>')
    return '\n'.join(svg)

def main():
    svg_content = generate_svg()
    svg_path = os.path.join(ROOT, 'docs', 'demo.svg')
    os.makedirs(os.path.dirname(svg_path), exist_ok=True)
    with open(svg_path, 'w', encoding='utf-8') as f:
        f.write(svg_content)
    print(f"SVG saved: {svg_path}")
    print(f"Size: {os.path.getsize(svg_path)} bytes")

if __name__ == '__main__':
    main()
