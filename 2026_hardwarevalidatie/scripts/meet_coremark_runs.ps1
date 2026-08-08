# Checkpoint 1: flash 04_coremark en draai n CoreMark-runs -> CSV.
# Gebruik: powershell -File D:\thesis\meet_coremark_runs.ps1 [-Runs 10]
param([int]$Runs = 10)
$env:TMP='D:\temp'; $env:TEMP='D:\temp'
. 'C:\Espressif\tools\Microsoft.v5.5.PowerShell_profile.ps1' | Out-Null

# Ruim processen op die COM3 kunnen bezetten (idf monitor e.d.)
Get-CimInstance Win32_Process |
  Where-Object { $_.CommandLine -match 'idf_monitor|idf\.py monitor' } |
  ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
Start-Sleep -Seconds 2

Set-Location 'D:\thesis\04_coremark'
Write-Host '=== FLASH 04_coremark (baseline, pass A+B) ==='
idf.py -p COM3 flash *> 'D:\thesis\04_coremark\flash_meetrun.log'
if ($LASTEXITCODE -ne 0) {
    Write-Host '=== FAIL flash, zie flash_meetrun.log ==='
    Get-Content 'D:\thesis\04_coremark\flash_meetrun.log' -Tail 30
    exit 1
}
Write-Host ("=== START " + $Runs + " runs (ca. 80 s per run) ===")
python D:\thesis\meet_coremark_runs.py COM3 $Runs
Write-Host '=== MEETSESSIE KLAAR ==='
