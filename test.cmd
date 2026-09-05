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

:: -- pure ops (always): op | fixture | golden-suffix | extra-arg --
for %%C in (
    "jesc     escape_in.txt   jesc"
    "junesc   golden_jesc.txt junesc"
    "calign   csv_in.csv      calign"
    "ccompact golden_calign.txt ccompact"
    "ccomma   csv_in.csv      ccomma"
    "csort    csv_in.csv      csort    1"
    "ctrans   csv_in.csv      ctrans"
    "ctojson  csv_in.csv      ctojson"
) do (
    for /f "tokens=1,2,3,4" %%a in (%%C) do (
        .\build\harness\test_harness.exe tests\%%b %%a %%d > tests\actual_%%c.txt
        fc /a tests\golden_%%c.txt tests\actual_%%c.txt >nul
        if errorlevel 1 ( echo MISMATCH: %%c & set FAIL=1 ) else ( echo OK: %%c )
    )
)

:: -- libyaml-backed ops (only when libyaml is vendored) --
if exist "vendor\libyaml\src\api.c" (
    for %%C in (
        "validate convert.yaml validate"
        "tojson   convert.yaml tojson"
        "toyaml   convert.json toyaml"
        "jpretty  json_in.json jpretty"
        "jmin     json_in.json jmin"
        "jsort    json_in.json jsort"
        "jtocsv   arr.json     jtocsv"
    ) do (
        for /f "tokens=1,2,3" %%a in (%%C) do (
            .\build\harness\test_harness.exe tests\%%b %%a > tests\actual_%%c.txt
            fc /a tests\golden_%%c.txt tests\actual_%%c.txt >nul
            if errorlevel 1 ( echo MISMATCH: %%c & set FAIL=1 ) else ( echo OK: %%c )
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
