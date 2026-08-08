$env:TMP='D:\temp'; $env:TEMP='D:\temp'
. 'C:\Espressif\tools\Microsoft.v5.5.PowerShell_profile.ps1' | Out-Null

Get-CimInstance Win32_Process |
  Where-Object { $_.CommandLine -match 'idf_monitor|idf\.py monitor' } |
  ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
Start-Sleep -Seconds 2

Set-Location 'D:\thesis\06_foutinjectie'
Write-Host '=== SET-TARGET + BUILD 06_foutinjectie ==='
idf.py set-target esp32s3 *> 'D:\thesis\06_foutinjectie\settarget.log'
if ($LASTEXITCODE -ne 0) { Write-Host 'FAIL set-target'; Get-Content settarget.log -Tail 20; exit 1 }
idf.py build *> 'D:\thesis\06_foutinjectie\build.log'
if ($LASTEXITCODE -ne 0) { Write-Host 'FAIL build'; Get-Content build.log -Tail 40; exit 1 }
Write-Host '=== FLASH ==='
idf.py -p COM3 flash *> 'D:\thesis\06_foutinjectie\flash.log'
if ($LASTEXITCODE -ne 0) { Write-Host 'FAIL flash'; Get-Content flash.log -Tail 30; exit 1 }
Write-Host '=== CAPTURE (20s) ==='
python D:\thesis\capture_generic.py COM3 20 D:\thesis\06_foutinjectie\foutinjectie_log_20260808.txt
Write-Host '=== PARSE -> CSV ==='
python D:\thesis\parse_foutinjectie.py D:\thesis\06_foutinjectie\foutinjectie_log_20260808.txt D:\thesis\06_foutinjectie\foutinjectie_campagneA_20260808.csv
Write-Host '=== KLAAR ==='
