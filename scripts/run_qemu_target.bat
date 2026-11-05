@echo off
setlocal enabledelayedexpansion

echo ==============================================================================
echo [QNX-CORTEXCAGE] Booting QNX 8.0 Microkernel Image in Headless QEMU
echo Target: ifs\cortex_x86_64.ifs (Multiboot)
echo Mode: Headless Serial Telemetry Streamer (-nographic -serial stdio)
echo ==============================================================================

set PROJECT_ROOT=%~dp0..
cd /d "%PROJECT_ROOT%"

:: Ensure QEMU is in PATH
set PATH=C:\Program Files\QEMU;C:\Users\WALTON\qemu;%PATH%

where qemu-system-x86_64.exe >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] qemu-system-x86_64.exe not found in PATH or C:\Program Files\QEMU or C:\Users\WALTON\qemu.
    exit /b 1
)

:: Verify IFS image exists
if not exist "ifs\cortex_x86_64.ifs" (
    echo [WARN] ifs\cortex_x86_64.ifs not found! Building IFS now...
    call scripts\build_ifs.bat || exit /b 1
)

echo [QEMU] Launching QNX 8.0 x86_64 Virtual Target...
echo [TIP] Press Ctrl+C or close window to terminate.
echo ==============================================================================
qemu-system-x86_64.exe -kernel ifs\cortex_x86_64.ifs -cpu max -m 512M -net none -nographic -serial stdio -monitor none -no-reboot
