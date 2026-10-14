@echo off
REM ============================================================
REM  Flash App Only: 生成 App bin 后只烧 0x08010000 应用区
REM  不做全片擦除，Bootloader / Slot A meta / boot_valid 保持不变
REM  适用场景: 日常只改 App 代码的快速迭代
REM  注意: 原任务引用的 DP83848.hex 从未生成过, 已改为 elf->bin + 指定地址烧写
REM ============================================================

setlocal

set "STLINK=C:\Program Files (x86)\STMicroelectronics\STM32 ST-LINK Utility\ST-LINK Utility\ST-LINK_CLI.exe"
set "OBJCOPY=arm-none-eabi-objcopy"
set "APP_ELF=%~dp0..\build\Debug\DP83848.elf"
set "APP_BIN=%~dp0..\build\Debug\DP83848.bin"

echo.
echo ============================================
echo  Generating App BIN from ELF ...
echo ============================================
"%OBJCOPY%" -O binary "%APP_ELF%" "%APP_BIN%"
if %ERRORLEVEL% NEQ 0 (
    echo [FAIL] App objcopy failed
    exit /b 1
)
echo [OK] App BIN generated

echo.
echo ============================================
echo  Flashing App -^> 0x08010000 (no mass erase) ...
echo ============================================
"%STLINK%" -c SWD -P "%APP_BIN%" 0x08010000 -V -Rst
if %ERRORLEVEL% NEQ 0 (
    echo [FAIL] App flash failed
    exit /b 1
)

echo.
echo [OK] App flashed and MCU reset.
echo  Try: ping 172.16.1.69
echo.
