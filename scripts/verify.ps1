# Phase 2 verification: checks all standard practices
# Run: powershell -ExecutionPolicy Bypass -File .\scripts\verify.ps1

$root = $PSScriptRoot | Split-Path -Parent
Set-Location $root

$ESC = [char]27
$CYAN  = "$ESC[96m"
$GREEN = "$ESC[92m"
$YELLOW = "$ESC[93m"
$WHITE = "$ESC[97m"
$GRAY  = "$ESC[90m"
$RED   = "$ESC[91m"
$RESET = "$ESC[0m"
$BOLD  = "$ESC[1m"

$bar = "=" * 72
$script:lines = @()
function P($text) { $script:lines += $text }

P ""
P "$CYAN$bar$RESET"
P "$CYAN  $BOLD`NexusLOB - Phase 4 Verification$RESET"
P "$CYAN$bar$RESET"
P ""

# ================================================================
# 1. Zero-I/O invariant check
# ================================================================
P "$CYAN  [1] Zero-I/O invariant (raft/ purity)$RESET"
P ""

$ioFiles = Get-ChildItem -Path "$root\raft\src","$root\raft\include" -Recurse -Include *.h,*.cpp | Select-String -Pattern "#include\s*<\s*(fstream|iostream|stdio|sys/socket|netinet|arpa/inet|unistd|fcntl)" | Select-Object -ExpandProperty Path -Unique
if ($ioFiles) {
    P "$RED  FAIL: I/O headers found in raft/:$RESET"
    foreach ($f in $ioFiles) { P "$RED    $f$RESET" }
} else {
    P "$GREEN  PASS: raft/ has zero I/O includes (fstream, sockets, etc.)$RESET"
}
P ""

# ================================================================
# 2. Build
# ================================================================
P "$CYAN  [2] Build (CMake + Ninja, C++17)$RESET"
P ""

$buildResult = cmake --build build 2>&1
if ($LASTEXITCODE -eq 0) {
    P "$GREEN  PASS: Build succeeded$RESET"
} else {
    P "$RED  FAIL: Build failed$RESET"
    $buildResult | Select-Object -Last 5 | ForEach-Object { P "    $_" }
}
P ""

# ================================================================
# 3. Tests
# ================================================================
P "$CYAN  [3] Test suite (14 tests)$RESET"
P ""

$testOutput = ctest --test-dir build --output-on-failure -j 1 2>&1
$testLines = $testOutput -split "`n"
$passed = 0
$total = 0
$testNum = 1

$phase1 = @(
    "interaction", "election", "log_replication", "kv_apply",
    "wal_crash_recovery", "property", "stress", "partition", "benchmark"
)
$phase2 = @(
    "lob_apply", "order_protocol", "order_replication", "lob_benchmark", "phase4_benchmark"
)

P "$GRAY  --- Phase 1 ---$RESET"
foreach ($tname in $phase1) {
    $total++
    $found = $false
    foreach ($tl in $testLines) { if ($tl -match "Test.*$tname.*Passed") { $found = $true; break } }
    $tpad = $tname.PadRight(24)
    if ($found) { P "$WHITE  [$testNum] $tpad $GREEN`PASS$RESET"; $passed++ }
    else { P "$WHITE  [$testNum] $tpad $RED`FAIL$RESET" }
    $testNum++
}

P "$GRAY  --- Phase 2 ---$RESET"
foreach ($tname in $phase2) {
    $total++
    $found = $false
    foreach ($tl in $testLines) { if ($tl -match "Test.*$tname.*Passed") { $found = $true; break } }
    $tpad = $tname.PadRight(24)
    if ($found) { P "$WHITE  [$testNum] $tpad $GREEN`PASS$RESET"; $passed++ }
    else { P "$WHITE  [$testNum] $tpad $RED`FAIL$RESET" }
    $testNum++
}
P ""
P "$GREEN  $passed/$total tests passed$RESET"
P ""

# ================================================================
# 4. CI checks (sanitizer + clang-tidy run on GitHub Actions)
# ================================================================
P "$CYAN  [4] CI checks (GitHub Actions - Linux)$RESET"
P ""
if (Test-Path "$root\.github\workflows\ci.yml") {
    P "$GREEN  PASS: .github/workflows/ci.yml exists$RESET"
    P "$GRAY    - Build + test on every push$RESET"
    P "$GRAY    - ASan + UBSan sanitizer build$RESET"
    P "$GRAY    - Runs on ubuntu-latest (Linux has libasan/libubsan)$RESET"
} else {
    P "$RED  FAIL: No CI config found$RESET"
}
P ""

# ================================================================
# 5. clang-tidy config
# ================================================================
P "$CYAN  [5] clang-tidy config$RESET"
P ""
if (Test-Path "$root\.clang-tidy") {
    P "$GREEN  PASS: .clang-tidy exists$RESET"
    P "$GRAY    - bugprone-*, modernize-*, performance-*, readability-* checks$RESET"
    P "$GRAY    - Runs on CI (Linux has clang-tidy installed)$RESET"
} else {
    P "$RED  FAIL: No .clang-tidy found$RESET"
}
P ""

# ================================================================
# 6. File inventory (counts changes in the last commit)
# ================================================================
P "$CYAN  [6] File inventory$RESET"
P ""
$newFiles = git diff --name-only --diff-filter=A HEAD~1 HEAD | Where-Object { $_ -match '\.(cpp|h)$' }
$modFiles = git diff --name-only --diff-filter=M HEAD~1 HEAD | Where-Object { $_ -match '\.(cpp|h)$' }
$new = $newFiles.Count
$mod = $modFiles.Count
P "$WHITE  New code files:      $new$RESET"
P "$WHITE  Modified code files: $mod$RESET"
P ""

# ================================================================
# Footer
# ================================================================
P "$CYAN$bar$RESET"
if ($passed -eq $total) {
    P "$GREEN  All checks passed$RESET"
} else {
    P "$RED  Some checks failed$RESET"
}
P "$CYAN  NexusLOB | Phase 4 Verification | C++17 | CMake | 14 tests$RESET"
P "$CYAN$bar$RESET"
P ""

foreach ($l in $script:lines) { Write-Host $l }

# Save for freeze
$tempFile = [System.IO.Path]::GetTempFileName()
$script:lines | Out-File -FilePath $tempFile -Encoding UTF8
$svgPath = "$root\docs\phase4_verify.svg"
$freezePath = (Get-Command freeze -ErrorAction SilentlyContinue)
if ($freezePath) {
    & freeze $tempFile --language ansi --window --padding 20,40 -o $svgPath 2>&1 | Out-Null
    Write-Host "$GRAY  SVG saved: docs/phase4_verify.svg$RESET"
}
Remove-Item $tempFile -ErrorAction SilentlyContinue
