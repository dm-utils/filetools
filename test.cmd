@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

call .\build_harness.cmd
if errorlevel 1 exit /b 1

set FAIL=0

for %%P in (default wide) do (
    .\build\harness\test_harness.exe test_docs.yaml %%P > tests\actual_%%P.txt
    fc /a tests\golden_%%P.txt tests\actual_%%P.txt >nul
    if errorlevel 1 (
        echo MISMATCH: profile %%P differs from tests\golden_%%P.txt
        echo   run:  fc tests\golden_%%P.txt tests\actual_%%P.txt
        set FAIL=1
    ) else (
        echo OK: profile %%P matches golden output
    )
)

if !FAIL! neq 0 (
    echo.
    echo Output regressed. If the new output is correct, review it and copy
    echo tests\actual_*.txt over tests\golden_*.txt.
    exit /b 1
)

echo.
echo All reindent regression tests passed.
exit /b 0
