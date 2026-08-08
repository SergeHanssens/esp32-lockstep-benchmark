$env:TMP='D:\temp'; $env:TEMP='D:\temp'
. 'C:\Espressif\tools\Microsoft.v5.5.PowerShell_profile.ps1' | Out-Null

Get-CimInstance Win32_Process |
  Where-Object { $_.CommandLine -match 'idf_monitor|idf\.py monitor' } |
  ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
Start-Sleep -Seconds 2

Set-Location 'D:\thesis\05_lockstep_kern'
Write-Host '=== SET-TARGET + BUILD 05_lockstep_kern ==='
idf.py set-target esp32s3 *> 'D:\thesis\05_lockstep_kern\settarget.log'
if ($LASTEXITCODE -ne 0) {
    Write-Host '=== FAIL set-target ==='
    Get-Content 'D:\thesis\05_lockstep_kern\settarget.log' -Tail 20
    exit 1
}
idf.py build *> 'D:\thesis\05_lockstep_kern\build.log'
if ($LASTEXITCODE -ne 0) {
    Write-Host '=== FAIL build ==='
    Get-Content 'D:\thesis\05_lockstep_kern\build.log' -Tail 40
    exit 1
}
Write-Host '=== FLASH ==='
idf.py -p COM3 flash *> 'D:\thesis\05_lockstep_kern\flash.log'
if ($LASTEXITCODE -ne 0) {
    Write-Host '=== FAIL flash ==='
    Get-Content 'D:\thesis\05_lockstep_kern\flash.log' -Tail 30
    exit 1
}
Write-Host '=== CAPTURE (15s) ==='
python D:\thesis\capture_generic.py COM3 15 D:\thesis\05_lockstep_kern\lockstep_kern_log_20260808.txt
Write-Host '=== KLAAR ==='
