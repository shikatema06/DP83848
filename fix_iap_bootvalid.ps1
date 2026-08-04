# Fix BOOT_VALID_ADDR to sector 11 end (avoid net_config conflict)
$path = 'C:\ST_project\DP83848_IAP\Bootloader\flash_if.h'
$lines = [System.IO.File]::ReadAllLines($path, [System.Text.Encoding]::UTF8)
$changed = $false
for ($i = 0; $i -lt $lines.Length; $i++) {
    if ($lines[$i] -match 'BOOT_VALID_ADDR' -and $lines[$i] -match '0x080E0000U') {
        $lines[$i] = $lines[$i] -replace '0x080E0000U', '0x080FFFFCU'
        $changed = $true
    }
}
if ($changed) {
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllLines($path, $lines, $utf8NoBom)
    Write-Host "[ok] BOOT_VALID_ADDR -> 0x080FFFFCU"
} else {
    Write-Host "[skip] target line not found"
}
Select-String -Path $path -Pattern 'BOOT_VALID_ADDR'
