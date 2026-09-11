@echo off
rem 瞬截 / SwiftSnip 构建脚本：Release x64，静态链接，产出单个 exe
setlocal

set "VCVARS=D:\App\VS-BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    echo [ERROR] vcvars64.bat not found: %VCVARS%
    exit /b 1
)

call "%VCVARS%" >nul
if errorlevel 1 (
    echo [ERROR] failed to initialize Visual Studio environment
    exit /b 1
)

pushd "%~dp0"
if not exist "build" mkdir "build"

rc /nologo /fo "build\app.res" "res\app.rc"
if errorlevel 1 (
    popd
    echo [ERROR] resource compile failed
    exit /b 1
)

cl /nologo /std:c++17 /utf-8 /O2 /MT /W4 /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00 ^
   /Fobuild\ /Fe"build\SwiftSnip.exe" ^
   src\main.cpp src\dpi.cpp src\settings.cpp src\hotkey.cpp src\tray.cpp ^
   src\capture.cpp src\png_writer.cpp src\autostart.cpp src\overlay.cpp src\settings_win.cpp ^
   src\scroll_capture.cpp ^
   build\app.res ^
   /link /SUBSYSTEM:WINDOWS /MACHINE:X64 /MANIFEST:NO ^
   user32.lib gdi32.lib shell32.lib shlwapi.lib ole32.lib oleaut32.lib ^
   windowscodecs.lib comctl32.lib advapi32.lib msimg32.lib

if errorlevel 1 (
    popd
    echo [ERROR] build failed
    exit /b 1
)

popd
echo [OK] output: %~dp0build\SwiftSnip.exe
