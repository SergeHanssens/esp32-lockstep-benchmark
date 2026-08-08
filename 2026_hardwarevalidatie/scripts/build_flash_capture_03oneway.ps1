$env:TMP='D:\temp'; $env:TEMP='D:\temp'
. 'C:\Espressif\tools\Microsoft.v5.5.PowerShell_profile.ps1' | Out-Null

# Ruim eerst processen op die COM3 kunnen bezetten (idf monitor e.d.)
Get-CimInstance Win32_Process |
  Where-Object { $_.CommandLine -match 'idf_monitor|idf\.py monitor' } |
  ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
Start-Sleep -Seconds 2

Set-Location 'D:\thesis\03_pingpong_oneway'
Write-Host '=== BUILD 03_pingpong_oneway (met ronde-index) ==='
idf.py build *> 'D:\thesis\03_pingpong_oneway\build_rondeindex.log'
if ($LASTEXITCODE -ne 0) {
    Write-Host '=== FAIL build, zie build_rondeindex.log ==='
    Get-Content 'D:\thesis\03_pingpong_oneway\build_rondeindex.log' -Tail 30
    exit 1
}
Write-Host '=== FLASH 03_pingpong_oneway ==='
idf.py -p COM3 flash *> 'D:\thesis\03_pingpong_oneway\flash_rondeindex.log'
if ($LASTEXITCODE -ne 0) {
    Write-Host '=== FAIL flash, zie flash_rondeindex.log ==='
    Get-Content 'D:\thesis\03_pingpong_oneway\flash_rondeindex.log' -Tail 30
    exit 1
}
Write-Host '=== CAPTURE (15s) ==='
python D:\thesis\capture_generic.py COM3 15 D:\thesis\03_pingpong_oneway\oneway_rondeindex_log_20260808.txt
Write-Host '=== KLAAR ==='
