@echo off
rem NyxMap QRhi bench: makes test videos, runs the benches, writes report.txt
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0rhi_bench.ps1" %*
pause
