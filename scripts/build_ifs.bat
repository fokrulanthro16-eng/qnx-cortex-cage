@echo off
setlocal enabledelayedexpansion

echo ==============================================================================
echo [QNX-CORTEXCAGE] Generating Bootable QNX 8.0 Image File System (IFS)
echo Target: ifs\cortex_x86_64.ifs (Multiboot x86_64)
echo ==============================================================================

:: Initialize QNX SDP 8.0 Environment
if exist "C:\Users\WALTON\qnx800\qnxsdp-env.bat" (
    call C:\Users\WALTON\qnx800\qnxsdp-env.bat >nul 2>&1
    echo [OK] Loaded C:\Users\WALTON\qnx800\qnxsdp-env.bat
) else (
    echo [ERROR] QNX environment script not found at C:\Users\WALTON\qnx800\qnxsdp-env.bat
    exit /b 1
)

set PROJECT_ROOT=%~dp0..
cd /d "%PROJECT_ROOT%"

:: Verify required binaries exist
if not exist "bin\x86_64\cortex_gate_resmgr" (
    echo [WARN] Binaries not found in bin\x86_64. Invoking build_all.bat...
    call scripts\build_all.bat || exit /b 1
)

echo [MKIFS] Generating bootable IFS image: ifs\cortex_x86_64.ifs...
mkifs -v ifs\cortex_x86_64.build ifs\cortex_x86_64.ifs

if exist "ifs\cortex_x86_64.ifs" (
    echo.
    echo ==============================================================================
    echo [SUCCESS] QNX 8.0 Bootable IFS Image generated successfully!
    dir ifs\cortex_x86_64.ifs | findstr /I "cortex_x86_64.ifs"
    echo ==============================================================================
) else (
    echo [ERROR] mkifs failed to create ifs\cortex_x86_64.ifs
    exit /b 1
)
