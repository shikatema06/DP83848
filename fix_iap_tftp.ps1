# DP83848_IAP tftp_server.c CRC replacement patch
# Run: powershell -ExecutionPolicy Bypass -File "C:\ST_project\DP83848\fix_iap_tftp.ps1"
# Effect: replace CRC check block with vector-table validity check in the IAP copy

$ErrorActionPreference = 'Stop'

$path = 'C:\ST_project\DP83848_IAP\Bootloader\tftp_server.c'
if (-not (Test-Path $path)) {
    Write-Host ("ERROR: file not found: " + $path) -ForegroundColor Red
    exit 1
}

$lines = [System.IO.File]::ReadAllLines($path, [System.Text.Encoding]::UTF8)

# Locate: start = line containing "CRC32 over the whole App image in Flash"
#         end   = line containing "Upgrade failed" (kept)
$start = -1
$end = -1
for ($i = 0; $i -lt $lines.Length; $i++) {
    if ($start -eq -1 -and $lines[$i] -match 'CRC32 over the whole App image in Flash') {
        $start = $i
        continue
    }
    if ($start -ne -1 -and $end -eq -1 -and $lines[$i] -match 'Upgrade failed') {
        $end = $i
        break
    }
}

if ($start -lt 0 -or $end -lt 0) {
    Write-Host ("ERROR: CRC block not found (start=" + $start + ", end=" + $end + ")") -ForegroundColor Red
    Write-Host "File may already be modified, or pattern does not match." -ForegroundColor Yellow
    Write-Host "Current file: $path" -ForegroundColor Yellow
    exit 1
}

Write-Host ("Found CRC block: line " + ($start + 1) + " to line " + $end + " (0-based: " + $start + ".." + ($end - 1) + ")")

# Replacement block (indentation matches original: 8/12/16 spaces)
$newBlock = @(
    '        /* Vector-table validity check: MSP + Reset_Handler must look right */'
    '        {'
    '            uint32_t app_sp = *(volatile uint32_t *)USER_FLASH_FIRST_PAGE_ADDRESS;'
    '            uint32_t app_pc = *(volatile uint32_t *)(USER_FLASH_FIRST_PAGE_ADDRESS + 4U);'
    ''
    '            /* MSP in SRAM range, PC in app Flash, Thumb bit set */'
    '            if (app_sp >= 0x20000000U && app_sp <= 0x20020000U &&'
    '                app_pc >= USER_FLASH_FIRST_PAGE_ADDRESS &&'
    '                app_pc <  USER_FLASH_END_ADDRESS &&'
    '                (app_pc & 1U) == 1U)'
    '            {'
    '                /* Write boot_valid flag */'
    '                FLASH_If_Init();'
    '                FLASH_If_Erase_Sector(BOOT_VALID_ADDR);'
    '                {'
    '                    uint32_t magic = BOOT_VALID_MAGIC;'
    '                    uint32_t addr  = BOOT_VALID_ADDR;'
    '                    FLASH_If_Write((__IO uint32_t *)&addr, &magic, 1);'
    '                }'
    '                HAL_FLASH_Lock();'
    ''
    '                /* Jump to the new App */'
    '                SCB->VTOR = USER_FLASH_FIRST_PAGE_ADDRESS;'
    '                __set_MSP(app_sp);'
    '                ((void (*)(void))app_pc)();'
    '            }'
    '        }'
    ''
)

# Reassemble: before + newBlock + after (after starts from $end)
$before = if ($start -gt 0) { $lines[0..($start-1)] } else { @() }
$after  = if ($end -le $lines.Length - 1) { $lines[$end..($lines.Length-1)] } else { @() }
$newLines = $before + $newBlock + $after

# Write back as UTF-8 without BOM
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllLines($path, $newLines, $utf8NoBom)
Write-Host "[ok] tftp_server.c CRC block replaced"

Write-Host ""
Write-Host "==== Verification ====" -ForegroundColor Cyan
Select-String -Path $path -Pattern 'crc32_calc|Vector-table validity|Upgrade failed' | ForEach-Object { Write-Host $_.ToString() }
