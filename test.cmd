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

:: -- pure string ops (always) --
for %%C in ("escape_in.txt jesc" "golden_jesc.txt junesc") do (
    for /f "tokens=1,2" %%a in (%%C) do (
        .\build\harness\test_harness.exe tests\%%a %%b > tests\actual_%%b.txt
        fc /a tests\golden_%%b.txt tests\actual_%%b.txt >nul
        if errorlevel 1 ( echo MISMATCH: %%b & set FAIL=1 ) else ( echo OK: %%b )
    )
)

:: -- libyaml-backed ops (only when libyaml is vendored) --
if exist "vendor\libyaml\src\api.c" (
    for %%C in ("convert.yaml validate" "convert.yaml tojson" "convert.json toyaml" "json_in.json jpretty" "json_in.json jmin" "json_in.json jsort") do (
        for /f "tokens=1,2" %%a in (%%C) do (
            .\build\harness\test_harness.exe tests\%%a %%b > tests\actual_%%b.txt
            fc /a tests\golden_%%b.txt tests\actual_%%b.txt >nul
            if errorlevel 1 ( echo MISMATCH: %%b & set FAIL=1 ) else ( echo OK: %%b )
        )
    )
) else (
    echo SKIP: libyaml tests ^(not vendored^)
)

if !FAIL! neq 0 (
    echo.
    echo Output regressed. If the new output is correct, review it and copy
    echo tests\actual_*.txt over tests\golden_*.txt.
    exit /b 1
)

echo.
echo All regression tests passed.
exit /b 0
