@echo off
cd /d "%~dp0"
if not exist "LJOverlay_new.exe" (
    echo LJOverlay_new.exe not found. Run BUILD_EXE.bat first.
    pause
    exit /b 1
)
start "LJ Overlay Modern Menu" "%~dp0LJOverlay_new.exe"
