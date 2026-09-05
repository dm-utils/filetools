@echo off
setlocal
cd /d "%~dp0"

:: -- Find Visual Studio --
set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist %VSWHERE% ( echo vswhere.exe not found & exit /b 1 )

for /f "usebackq tokens=*" %%i in (
    `%VSWHERE% -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`
) do set VS_PATH=%%i
if "%VS_PATH%"=="" ( echo VS C++ tools not found & exit /b 1 )

call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1

mkdir build 2>nul

:: -- Compile resource --
rc.exe /nologo /fo build\settings.res src\settings.rc
if errorlevel 1 ( echo Resource compile failed & exit /b 1 )

:: -- libyaml (optional, see vendor/VENDORING.md) --
set "YAMLFLAGS="
set "YAMLSRC="
if exist "vendor\libyaml\src\api.c" (
    echo Using vendored libyaml.
    set "YAMLFLAGS=/I vendor /I vendor\libyaml\include /DHAVE_CONFIG_H /DYAML_DECLARE_STATIC /DHAVE_LIBYAML"
    set "YAMLSRC=vendor\libyaml\src\*.c"
)

:: -- Compile and link (options before sources to avoid D9026) --
cl /LD /O2 /EHsc /std:c++17 /MT /utf-8 %YAMLFLAGS% ^
   src\dllmain.cpp src\yaml_tidy.cpp src\yaml_convert.cpp src\json_tools.cpp src\csv_tools.cpp %YAMLSRC% ^
   build\settings.res ^
   /Fe:build\FileTools.dll ^
   /Fo:build\ ^
   /link user32.lib shell32.lib
if errorlevel 1 ( echo Build failed & exit /b 1 )

:: -- Close Notepad++ --
taskkill /f /im notepad++.exe >nul 2>&1
ping -n 2 127.0.0.1 >nul 2>&1

:: -- Deploy DLL (and remove the old YamlTools plugin folder) --
rmdir /s /q "C:\Program Files\Notepad++\plugins\YamlTools" 2>nul
set DST=C:\Program Files\Notepad++\plugins\FileTools
if not exist "%DST%" mkdir "%DST%"
copy /y "build\FileTools.dll" "%DST%\FileTools.dll"
if errorlevel 1 ( echo Copy failed & exit /b 1 )
copy /y "src\help.txt" "%DST%\help.txt"
if errorlevel 1 ( echo Help file copy failed & exit /b 1 )

:: -- Restart Notepad++ --
start "" "C:\Program Files\Notepad++\notepad++.exe"

echo Build and deploy complete.
exit /b 0
