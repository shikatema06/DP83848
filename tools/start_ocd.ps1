$env:PATH += ";C:\ST\openocd\DevEnv\openocd-v0.12.0-i686-w64-mingw32\bin"
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = "openocd.exe"
$psi.Arguments = "-f C:\ST_project\DP83848\tools\openocd_boot.cfg"
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$p = [System.Diagnostics.Process]::Start($psi)
$p.WaitForExit(8000)
$out = $p.StandardOutput.ReadToEnd()
$err = $p.StandardError.ReadToEnd()
[System.IO.File]::WriteAllText("C:\ST_project\DP83848\tools\ocd.log", $out)
[System.IO.File]::WriteAllText("C:\ST_project\DP83848\tools\ocd_err.log", $err)
Write-Host "DONE"
