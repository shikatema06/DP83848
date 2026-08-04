# Fix BOOT_VALID_ADDR reference in app code to match new sector 11 location
$path = 'C:\ST_project\DP83848_IAP\Core\Src\tcp_echoserver.c'
$lines = [System.IO.File]::ReadAllLines($path, [System.Text.Encoding]::UTF8)
$oldAddr = '0x0800FFFCU'
$newAddr = '0x080FFFFCU'
$count = 0
for ($i = 0; $i -lt $lines.Length; $i++) {
    if ($lines[$i].Contains($oldAddr)) {
        $lines[$i] = $lines[$i].Replace($oldAddr, $newAddr)
        $count++
    }
}
if ($count -gt 0) {
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllLines($path, $lines, $utf8NoBom)
    Write-Host ("[ok] replaced " + $count + " occurrence(s) of " + $oldAddr + " -> " + $newAddr)
} else {
    Write-Host "[skip] address not found in file"
}
Write-Host ""
Write-Host "==== Verification ====" -ForegroundColor Cyan
Select-String -Path $path -Pattern '0x080FFFC|0x0800FFFC|boot_valid|FLASH_Program' | ForEach-Object { Write-Host $_.ToString() }
