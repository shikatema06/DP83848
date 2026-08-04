@echo off
REM ============================================================
REM  Flash Both: Bootloader (with boot_valid embedded) + App
REM  Requires: STM32 ST-LINK Utility CLI in PATH or default dir
REM  Key fix: boot_valid is PATCHED into Bootloader.hex so
REM  it stays correct regardless of flash erase order.
REM ============================================================

setlocal enabledelayedexpansion

set "STLINK=C:\Program Files (x86)\STMicroelectronics\STM32 ST-LINK Utility\ST-LINK Utility\ST-LINK_CLI.exe"
set "OBJCOPY=arm-none-eabi-objcopy"
set "BUILD_DIR=%~dp0..\build\Debug"
set "PATCH_SCRIPT=%~dp0patch_hex.py"

echo.
echo ============================================
echo  Generating Bootloader HEX from ELF ...
echo ============================================
"%OBJCOPY%" -O ihex "%BUILD_DIR%\DP83848_BOOT.elf" "%BUILD_DIR%\DP83848_BOOT.hex"
if %ERRORLEVEL% NEQ 0 (
    echo [FAIL] objcopy failed
    exit /b 1
)
echo [OK] Bootloader HEX generated

echo.
echo ============================================
echo  Patching boot_valid into Bootloader HEX ...
echo ============================================
python "%PATCH_SCRIPT%" "%BUILD_DIR%\DP83848_BOOT.hex"
if %ERRORLEVEL% NEQ 0 (
    echo [FAIL] Hex patch failed
    exit /b 1
)

echo.
echo ============================================
echo  STEP 1/4: Erasing all Flash ...
echo ============================================
"%STLINK%" -c SWD -ME
if %ERRORLEVEL% NEQ 0 (
    echo [FAIL] Erase failed
    exit /b 1
)

echo.
echo ============================================
echo  STEP 2/4: Flashing Bootloader -^> 0x08000000 ...
echo ============================================
REM Bootloader.hex now includes boot_valid=0xAA55AA55 at 0x0800FFFC (end of sector 3)
"%STLINK%" -c SWD -P "%BUILD_DIR%\DP83848_BOOT.hex" -V
if %ERRORLEVEL% NEQ 0 (
    echo [FAIL] Bootloader flash failed
    exit /b 1
)

echo.
echo ============================================
echo  STEP 3/4: Flashing App -^> 0x08010000 ...
echo ============================================
"%STLINK%" -c SWD -P "%BUILD_DIR%\DP83848.hex" -V
if %ERRORLEVEL% NEQ 0 (
    echo [FAIL] App flash failed
    exit /b 1
)

echo.
echo ============================================
echo  STEP 4/4: Resetting MCU ...
echo ============================================
"%STLINK%" -c SWD -Rst
if %ERRORLEVEL% NEQ 0 (
    echo [FAIL] Reset failed
    exit /b 1
)

echo.
echo ============================================
echo  SUCCESS - Device is ready
echo  Bootloader: 0x08000000  (64KB)
echo  App:        0x08010000  (832KB)
echo  boot_valid: 0xAA55AA55 @ 0x0800FFFC  (embedded in Bootloader)
echo ============================================
echo.
echo  After reset, Bootloader jumps to App.
echo  App IP: 172.16.1.69 (from net_config defaults)
echo  Try: ping 172.16.1.69
echo.
