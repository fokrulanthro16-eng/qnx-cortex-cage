@echo off
setlocal enabledelayedexpansion

echo ==============================================================================
echo [QNX-CORTEXCAGE] One-Click Turnkey Hackathon Judge Demo Launcher
echo Microkernel-Enforced Deterministic Simplex Safety Cage for Physical AI
echo ==============================================================================

set PROJECT_ROOT=%~dp0..
cd /d "%PROJECT_ROOT%"

:: Step 1: Compile binaries & verify ELF symbols
echo [1/4] Compiling dual-arch binaries (x86_64 + ARM64)...
call scripts\build_all.bat || exit /b 1

:: Step 2: Build bootable QNX 8.0 IFS image
echo [2/4] Generating bootable QNX 8.0 Image File System...
call scripts\build_ifs.bat || exit /b 1

:: Step 3: Install Cockpit dependencies if missing
echo [3/4] Preparing Mission Control Cockpit Server...
cd /d "%PROJECT_ROOT%\cockpit"
if not exist "node_modules" call npm install --silent

:: Step 4: Launch Cockpit & Target
echo [4/4] Launching Mission Control Cockpit (http://localhost:3080)...
start http://localhost:3080

echo.
echo ==============================================================================
echo [READY] Mission Control Cockpit is online at: http://localhost:3080
echo [READY] Interactive QNX 8.0 Codelab is at:   http://localhost:3080/codelab.html
echo ==============================================================================
echo [COCKPIT] Starting Cockpit Server with Integrated QNX Target Bridge...
node server.js
