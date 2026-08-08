# Checkpoint 1: flash 04_coremark en draai n CoreMark-runs -> CSV.
# Gebruik: powershell -File meet_coremark_runs.ps1 [-Runs 10] [-Poort COM3]
#          [-ProjectMap <pad naar 04_coremark>] [-IdfProfiel <pad ps1-profiel>]
param(
    [int]$Runs = 10,
    [string]$Poort = 'COM3',
    [string]$ProjectMap = 'D:\thesis\04_coremark',
    [string]$MeetScript = (Join-Path $PSScriptRoot 'meet_coremark_runs.py'),
    [string]$IdfProfiel = 'C:\Espressif\tools\Microsoft.v5.5.PowerShell_profile.ps1'
)
$env:TMP='D:\temp'; $env:TEMP='D:\temp'
. $IdfProfiel | Out-Null

# Ruim processen op die de poort kunnen bezetten (idf monitor e.d.)
Get-CimInstance Win32_Process |
  Where-Object { $_.CommandLine -match 'idf_monitor|idf\.py monitor' } |
  ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
Start-Sleep -Seconds 2

Set-Location $ProjectMap
Write-Host "=== FLASH $ProjectMap (baseline, pass A+B) ==="
idf.py -p $Poort flash *> (Join-Path $ProjectMap 'flash_meetrun.log')
if ($LASTEXITCODE -ne 0) {
    Write-Host '=== FAIL flash, zie flash_meetrun.log ==='
    Get-Content (Join-Path $ProjectMap 'flash_meetrun.log') -Tail 30
    exit 1
}
Write-Host ("=== START " + $Runs + " runs (ca. 80 s per run) ===")
python $MeetScript $Poort $Runs
Write-Host '=== MEETSESSIE KLAAR ==='
