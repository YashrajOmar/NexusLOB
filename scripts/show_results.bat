@echo off
echo.
echo ================================================================
echo                    RaftKVStore - Test Suite
echo ================================================================
echo.
ctest --test-dir build --output-on-failure
echo.
echo ================================================================
echo                    RaftKVStore - Benchmark
echo ================================================================
echo.
build\test_benchmark.exe
echo.
echo ================================================================
echo                    RaftKVStore - Live Demo
echo ================================================================
echo.
powershell -ExecutionPolicy Bypass -File scripts\full_demo.ps1
echo.
echo ================================================================
pause
