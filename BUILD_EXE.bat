@echo off
setlocal EnableExtensions
cd /d "%~dp0"
chcp 65001 >nul

echo Checking project and Windows compiler...
call :BUILD > "%~dp0BUILD_LOG.txt" 2>&1
set "RESULT=%ERRORLEVEL%"
type "%~dp0BUILD_LOG.txt"
echo.
if not "%RESULT%"=="0" (
  echo FAILED. Send the file BUILD_LOG.txt, not only the last screenshot.
  echo No new executable was produced by this build.
) else (
  echo SUCCESS: %~dp0LJOverlay_new.exe
  echo Old overlay processes were stopped automatically before compilation.
)
pause
exit /b %RESULT%

:BUILD
echo ===== LJOverlay Modern Menu / Windows build =====
echo Current directory: %CD%
if not exist "lj_overlay.cpp" (
  echo ERROR: lj_overlay.cpp not found. Extract the ZIP before running this file.
  exit /b 2
)

rem Always use the full UCRT64 compiler installation, not random g++ from PATH.
set "MSYS_ROOT=%SystemDrive%\msys64"
if not exist "%MSYS_ROOT%\ucrt64\bin\g++.exe" if exist "D:\msys64\ucrt64\bin\g++.exe" set "MSYS_ROOT=D:\msys64"
if not exist "%MSYS_ROOT%\ucrt64\bin\g++.exe" if exist "%ProgramFiles%\MSYS2\ucrt64\bin\g++.exe" set "MSYS_ROOT=%ProgramFiles%\MSYS2"
set "UCRT_BIN=%MSYS_ROOT%\ucrt64\bin"
set "MSYS_BIN=%MSYS_ROOT%\usr\bin"
if not exist "%UCRT_BIN%\g++.exe" (
  echo ERROR: MSYS2 UCRT64 g++ not found at %UCRT_BIN%\g++.exe
  echo Install MSYS2 from https://www.msys2.org/ , open "MSYS2 UCRT64" and run:
  echo   pacman -Syu
  echo   pacman -S --needed mingw-w64-ucrt-x86_64-gcc
  echo Close that terminal and run BUILD_EXE.bat again.
  exit /b 3
)

rem This fixes cc1plus.exe missing DLLs. Do NOT hardcode versioned DLL names.
set "PATH=%UCRT_BIN%;%MSYS_BIN%;%PATH%"
set "MSYSTEM=UCRT64"
echo Using: %UCRT_BIN%\g++.exe
"%UCRT_BIN%\g++.exe" --version
if errorlevel 1 (
  echo ERROR: g++ itself could not start; install/repair UCRT64 GCC with pacman.
  exit /b 4
)

echo Closing old LJOverlay processes before compilation...
if not exist "%~dp0STOP_OLD_OVERLAY.ps1" (
  echo ERROR: STOP_OLD_OVERLAY.ps1 not found. Restore this project file.
  exit /b 7
)
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0STOP_OLD_OVERLAY.ps1"
if errorlevel 1 (
  echo ERROR: Could not stop an old overlay. Compilation was not started.
  exit /b 7
)

echo Compiling full GUI source (not just the LJ_TEST parser)...
"%UCRT_BIN%\g++.exe" -std=c++20 -O2 -municode -mwindows -static -static-libgcc -static-libstdc++ "lj_overlay.cpp" -o "LJOverlay_new.exe" -lgdi32 -luser32 -lshell32 -ladvapi32 -lwinmm
if errorlevel 1 (
  echo ERROR: C++ compiler/linker error. See lines above and share BUILD_LOG.txt.
  exit /b 5
)
if not exist "LJOverlay_new.exe" (
  echo ERROR: Compiler finished, but LJOverlay_new.exe does not exist.
  exit /b 6
)
echo OK: executable created.
exit /b 0
