$env:TMP='D:\temp'; $env:TEMP='D:\temp'
. 'C:\Espressif\tools\Microsoft.v5.5.PowerShell_profile.ps1' | Out-Null

Get-CimInstance Win32_Process |
  Where-Object { $_.CommandLine -match 'idf_monitor|idf\.py monitor' } |
  ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
Start-Sleep -Seconds 2

Set-Location 'D:\thesis\07_coremark_lockstep'
Write-Host '=== SET-TARGET + BUILD 07_coremark_lockstep (VALIDATION-SEEDS 0x3415) ==='
idf.py set-target esp32s3 *> 'D:\thesis\07_coremark_lockstep\settarget_val.log'
if ($LASTEXITCODE -ne 0) {
    Write-Host '=== FAIL set-target ==='
    Get-Content 'D:\thesis\07_coremark_lockstep\settarget_val.log' -Tail 20
    exit 1
}
idf.py -D LS_VALIDATION_RUN=1 build *> 'D:\thesis\07_coremark_lockstep\build_val.log'
if ($LASTEXITCODE -ne 0) {
    Write-Host '=== FAIL build ==='
    Get-Content 'D:\thesis\07_coremark_lockstep\build_val.log' -Tail 40
    exit 1
}
Write-Host '=== FLASH ==='
idf.py -p COM3 flash *> 'D:\thesis\07_coremark_lockstep\flash_val.log'
if ($LASTEXITCODE -ne 0) {
    Write-Host '=== FAIL flash ==='
    Get-Content 'D:\thesis\07_coremark_lockstep\flash_val.log' -Tail 30
    exit 1
}
Write-Host '=== CAPTURE (200s: 5 fasen, validation-seeds 0x3415) ==='
$stamp = Get-Date -Format yyyyMMdd
python D:\thesis\capture_generic.py COM3 200 "D:\thesis\07_coremark_lockstep\coremark_lockstep_validation0x3415_log_$stamp.txt"
Write-Host '=== KLAAR ==='
