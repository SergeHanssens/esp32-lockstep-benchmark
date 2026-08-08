$env:TMP='D:\temp'; $env:TEMP='D:\temp'
. 'C:\Espressif\tools\Microsoft.v5.5.PowerShell_profile.ps1' | Out-Null

# Ruim eerst processen op die COM3 kunnen bezetten (idf monitor e.d.)
Get-CimInstance Win32_Process |
  Where-Object { $_.CommandLine -match 'idf_monitor|idf\.py monitor' } |
  ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
Start-Sleep -Seconds 2

$jobs = @(
  @{ p='03_pingpong_oneway_rtos'; log='D:\thesis\03_pingpong_oneway_rtos\oneway_rtos_log_20260808.txt'; sec=15 },
  @{ p='04_coremark_max_RTOS';    log='D:\thesis\04_coremark_max_RTOS\coremark_max_rtos_log_20260808.txt'; sec=75 },
  @{ p='04_coremark_min_RTOS';    log='D:\thesis\04_coremark_min_RTOS\coremark_min_rtos_log_20260808.txt'; sec=75 }
)
foreach ($j in $jobs) {
    Set-Location ("D:\thesis\" + $j.p)
    Write-Host ("=== FLASH " + $j.p + " ===")
    idf.py -p COM3 flash *> ("D:\thesis\" + $j.p + "\flash.log")
    if ($LASTEXITCODE -ne 0) { Write-Host ("=== FAIL flash " + $j.p + " ==="); continue }
    Write-Host ("=== CAPTURE " + $j.p + " (" + $j.sec + "s) ===")
    python D:\thesis\capture_generic.py COM3 $j.sec $j.log
    Write-Host ("=== KLAAR " + $j.p + " ===")
}
Write-Host "=== ALLES KLAAR ==="
