# DP83848_IAP 修改脚本
# 用法: 在 PowerShell 里跑这段 (复制粘贴到 PowerShell 窗口)
# 作用: 在 C:\ST_project\DP83848_IAP 里改 4 个文件, 不动原工程

$ErrorActionPreference = 'Stop'

$root = 'C:\ST_project\DP83848_IAP'
if (-not (Test-Path $root)) {
    Write-Host "ERROR: $root 不存在, 请先把 C:\ST_project\DP83848 复制成 $root" -ForegroundColor Red
    exit 1
}

# ---- 1. 改链接脚本: FLASH 起点 0x08010000 ----
$f = Join-Path $root 'STM32F407XX_APP.ld'
(Get-Content $f -Raw -Encoding UTF8) `
    -replace 'FLASH\s+\(rx\)\s+:\s+ORIGIN\s+=\s+0x08000000,\s+LENGTH\s+=\s+1024K', `
             'FLASH  (rx)  : ORIGIN = 0x08010000, LENGTH = 832K' |
    Set-Content $f -Encoding UTF8 -NoNewline
Write-Host "[1/4] 改完 STM32F407XX_APP.ld"

# ---- 2. 改 main.c: VTOR 同步到 0x08010000 ----
$f = Join-Path $root 'Core\Src\main.c'
(Get-Content $f -Raw -Encoding UTF8) `
    -replace 'SCB->VTOR = 0x08000000U;', `
             'SCB->VTOR = 0x08010000U;  /* app starts at 0x08010000 when bootloader is present */' |
    Set-Content $f -Encoding UTF8 -NoNewline
Write-Host "[2/4] 改完 main.c"

# ---- 3. 改 flash_if.h: BOOT_VALID_ADDR 挪到 sector 11 ----
$f = Join-Path $root 'Bootloader\flash_if.h'
(Get-Content $f -Raw -Encoding UTF8) `
    -replace '#define BOOT_VALID_ADDR\s+0x0800FFFCU', `
             '#define BOOT_VALID_ADDR                0x080E0000U  /* sector 11, 避开 bootloader/app 代码区 */' |
    Set-Content $f -Encoding UTF8 -NoNewline
Write-Host "[3/4] 改完 flash_if.h"

# ---- 4. 改 tftp_server.c: 砍掉 CRC 校验, 改成向量表合法性检查 ----
#    改动较大, 直接替换整段 wrq_recv_callback 里的最后一块
$f = Join-Path $root 'Bootloader\tftp_server.c'
$old = @'
        /* CRC32 over the whole App image in Flash */
        {
            uint32_t img_size = (uint32_t)args->tot_bytes;
            uint32_t calc_crc;

            /* Last 4 bytes in the received image are the expected CRC32 */
            if (img_size > 4U) {
                uint32_t expected_crc;
                uint32_t fw_size = img_size - 4U;

                /* Read expected CRC from end of received data in Flash */
                expected_crc = *(volatile uint32_t *)(Flash_Write_Address - 4U);

                /* Compute CRC32 over firmware portion (excluding CRC itself) */
                calc_crc = crc32_calc((const uint8_t *)USER_FLASH_FIRST_PAGE_ADDRESS, fw_size);

                if (calc_crc == expected_crc && fw_size > 0) {
                    /* Write boot_valid flag */
                    FLASH_If_Init();
                    FLASH_If_Erase_Sector(BOOT_VALID_ADDR);
                    {
                        uint32_t magic = BOOT_VALID_MAGIC;
                        uint32_t addr  = BOOT_VALID_ADDR;
                        FLASH_If_Write((__IO uint32_t *)&addr, &magic, 1);
                    }
                    HAL_FLASH_Lock();

                    /* Jump directly to the new App — no reset needed */
                    {
                        uint32_t app_sp = *(volatile uint32_t *)USER_FLASH_FIRST_PAGE_ADDRESS;
                        uint32_t app_pc = *(volatile uint32_t *)(USER_FLASH_FIRST_PAGE_ADDRESS + 4U);
                        if (app_sp >= 0x20000000U && app_sp <= 0x20020000U) {
                            SCB->VTOR = USER_FLASH_FIRST_PAGE_ADDRESS;
                            __set_MSP(app_sp);
                            ((void (*)(void))app_pc)();
                        }
                    }
                }
            }
        }

        /* Upgrade failed (CRC mismatch or zero-length) — reset and retry */
        NVIC_SystemReset();
        while (1) {}
'@

$new = @'
        /* Vector-table validity check: MSP + Reset_Handler must look right */
        {
            uint32_t app_sp = *(volatile uint32_t *)USER_FLASH_FIRST_PAGE_ADDRESS;
            uint32_t app_pc = *(volatile uint32_t *)(USER_FLASH_FIRST_PAGE_ADDRESS + 4U);

            /* MSP in SRAM range, PC in app Flash, Thumb bit set */
            if (app_sp >= 0x20000000U && app_sp <= 0x20020000U &&
                app_pc >= USER_FLASH_FIRST_PAGE_ADDRESS &&
                app_pc <  USER_FLASH_END_ADDRESS &&
                (app_pc & 1U) == 1U)
            {
                /* Write boot_valid flag */
                FLASH_If_Init();
                FLASH_If_Erase_Sector(BOOT_VALID_ADDR);
                {
                    uint32_t magic = BOOT_VALID_MAGIC;
                    uint32_t addr  = BOOT_VALID_ADDR;
                    FLASH_If_Write((__IO uint32_t *)&addr, &magic, 1);
                }
                HAL_FLASH_Lock();

                /* Jump to the new App */
                SCB->VTOR = USER_FLASH_FIRST_PAGE_ADDRESS;
                __set_MSP(app_sp);
                ((void (*)(void))app_pc)();
            }
        }

        /* Upgrade failed (invalid vector table) — reset and retry */
        NVIC_SystemReset();
        while (1) {}
'@

(Get-Content $f -Raw -Encoding UTF8).Replace($old, $new) | Set-Content $f -Encoding UTF8 -NoNewline
Write-Host "[4/4] 改完 tftp_server.c"

Write-Host ""
Write-Host "==== 验证改动 ====" -ForegroundColor Cyan
Select-String -Path (Join-Path $root 'STM32F407XX_APP.ld') -Pattern 'FLASH\s+\(rx\)'
Select-String -Path (Join-Path $root 'Core\Src\main.c') -Pattern 'VTOR\s*='
Select-String -Path (Join-Path $root 'Bootloader\flash_if.h') -Pattern 'BOOT_VALID_ADDR'
Select-String -Path (Join-Path $root 'Bootloader\tftp_server.c') -Pattern 'crc32_calc|Vector-table'

Write-Host ""
Write-Host "==== 副本状态 ====" -ForegroundColor Cyan
Get-ChildItem $root | Select-Object Name, Length | Format-Table -AutoSize
