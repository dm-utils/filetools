@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

call .\build_harness.cmd %1
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
    "cquote   csv_in.csv      cquote"
    "cunquote golden_cquote.txt cunquote"
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

:: -- CLI (filetools.exe): same engines, so compare against the same goldens --
set CLI=.\build\harness\filetools.exe
set CLIOPT=--no-ini
for %%C in (
    "yaml-tidy        test_docs.yaml      default"
    "json-to-yaml     tests\convert.json  toyaml"
    "csv-align        tests\csv_in.csv    calign"
    "csv-to-comma     tests\csv_in.csv    ccomma"
    "csv-transpose    tests\csv_in.csv    ctrans"
    "csv-add-quotes   tests\csv_in.csv    cquote"
    "json-to-csv      tests\arr.json      jtocsv"
) do (
    for /f "tokens=1,2,3" %%a in (%%C) do (
        %CLI% %%a %CLIOPT% %%b > tests\actual_cli_%%c.txt
        fc /a tests\golden_%%c.txt tests\actual_cli_%%c.txt >nul
        if errorlevel 1 ( echo MISMATCH: cli %%a & set FAIL=1 ) else ( echo OK: cli %%a )
    )
)
%CLI% csv-sort %CLIOPT% --col 2 tests\csv_in.csv > tests\actual_cli_csort.txt
fc /a tests\golden_csort.txt tests\actual_cli_csort.txt >nul
if errorlevel 1 ( echo MISMATCH: cli csv-sort & set FAIL=1 ) else ( echo OK: cli csv-sort )
type tests\csv_in.csv | %CLI% csv-to-comma %CLIOPT% > tests\actual_cli_stdin.txt
fc /a tests\golden_ccomma.txt tests\actual_cli_stdin.txt >nul
if errorlevel 1 ( echo MISMATCH: cli stdin & set FAIL=1 ) else ( echo OK: cli stdin )

:: batch: pattern with **, --check exit codes, --in-place, --out-dir, one bad file
set T=build\cli_test
rmdir /s /q %T% 2>nul
mkdir %T%\sub\deep
copy /y test_docs.yaml %T%\a.yaml >nul
copy /y test_docs.yaml %T%\sub\deep\b.yaml >nul
copy /y tests\convert.json %T%\sub\ok.json >nul
echo {"broken": > %T%\sub\bad.json
%CLI% yaml-tidy %CLIOPT% --check "%T%\**\*.yaml" >nul 2>&1
if errorlevel 1 ( echo OK: cli --check reports changes ) else ( echo FAIL: cli --check should exit 1 & set FAIL=1 )
%CLI% yaml-tidy %CLIOPT% --in-place "%T%\**\*.yaml" >nul 2>&1
if errorlevel 1 ( echo FAIL: cli --in-place & set FAIL=1 ) else ( echo OK: cli --in-place )
fc /a tests\golden_default.txt %T%\sub\deep\b.yaml >nul
if errorlevel 1 ( echo MISMATCH: cli --in-place result & set FAIL=1 ) else ( echo OK: cli --in-place result )
%CLI% yaml-tidy %CLIOPT% --check "%T%\**\*.yaml" >nul 2>&1
if errorlevel 1 ( echo FAIL: cli --check after tidy should exit 0 & set FAIL=1 ) else ( echo OK: cli --check clean )
%CLI% json-to-yaml %CLIOPT% --out-dir %T%\out "%T%\**\*.json" >nul 2>&1
if errorlevel 2 ( echo OK: cli bad file gives exit 2 ) else ( echo FAIL: cli bad file should exit 2 & set FAIL=1 )
fc /a tests\golden_toyaml.txt %T%\out\sub\ok.yaml >nul
if errorlevel 1 ( echo MISMATCH: cli --out-dir result & set FAIL=1 ) else ( echo OK: cli --out-dir keeps folder structure )

if !FAIL! neq 0 (
    echo.
    echo Output regressed. If the new output is correct, review it and copy
    echo tests\actual_*.txt over tests\golden_*.txt.
    exit /b 1
)

echo.
echo All regression tests passed.
exit /b 0
