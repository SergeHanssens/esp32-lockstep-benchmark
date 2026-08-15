param(
  [Parameter(Mandatory=$true)][string]$Port,
  [Parameter(Mandatory=$true)][string]$AdapterSerial,
  [Parameter(Mandatory=$true)][string]$OutLog,
  [int]$Seconds = 35
)
# Activeer IDF-omgeving
. C:\Espressif\tools\Microsoft.v5.5.PowerShell_profile.ps1 *> $null

$ocdScripts = "C:\Espressif\tools\openocd-esp32\v0.12.0-esp32-20250422\openocd-esp32\share\openocd\scripts"
$elf = "D:\thesis\ClaudeCode\esp32-lockstep-benchmark\2026_hardwarevalidatie\05_lockstep_kern\build\lockstep_kern.elf"
$gdbScript = "D:\thesis\ClaudeCode\jtag\inject.gdb"
$jtagDir = "D:\thesis\ClaudeCode\jtag"
$ocdLog = Join-Path $jtagDir ("openocd_{0}.log" -f $Port)

# 1) OpenOCD server op de achtergrond (selecteer bord via USB-serienummer).
#    Per-bord cfg-bestand vermijdt argument-splitsing op spaties.
$boardCfg = Join-Path $jtagDir ("board_{0}.cfg" -f $Port)
@("adapter serial $AdapterSerial", "source [find board/esp32s3-builtin.cfg]") |
    Set-Content -Path $boardCfg -Encoding ascii
$ocd = Start-Process -FilePath "openocd" `
  -ArgumentList @("-s", $ocdScripts, "-f", $boardCfg) `
  -RedirectStandardError $ocdLog -RedirectStandardOutput ($ocdLog + ".out") -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 4
if (-not (Test-NetConnection -ComputerName localhost -Port 3333 -WarningAction SilentlyContinue).TcpTestSucceeded) {
    Write-Output "!! OpenOCD luistert niet op 3333 - zie $ocdLog"
    Get-Content $ocdLog -ErrorAction SilentlyContinue | Select-Object -Last 8
}

# 2) Passieve seriele capture op de achtergrond
$capOut = $OutLog
$cap = Start-Process -FilePath "python" `
  -ArgumentList @("$jtagDir\passive_read.py", $Port, "$Seconds", $capOut) `
  -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 2

# 3) gdb batch: reset halt + drie JTAG-bitflips + vrije run tot einde
Write-Output "=== GDB injectie op $Port (adapter $AdapterSerial) ==="
& xtensa-esp32s3-elf-gdb --batch -nx $elf -x $gdbScript 2>&1 |
    Select-String -Pattern 'Breakpoint|Inferior|halt|remote|0x4200|Continuing|error|Error|Remote' |
    Select-Object -First 30

# 4) Wacht tot de seriele capture klaar is
Wait-Process -Id $cap.Id -Timeout ($Seconds + 15) -ErrorAction SilentlyContinue

# 5) OpenOCD opruimen
if ($ocd -and -not $ocd.HasExited) { Stop-Process -Id $ocd.Id -Force -ErrorAction SilentlyContinue }

Write-Output "=== SERIELE LOG ($Port) - detecties ==="
Get-Content $capOut -ErrorAction SilentlyContinue |
    Select-String -Pattern 'mismatch-tellers|gedetecteerd|zelftest detector|RESULTATEN'
