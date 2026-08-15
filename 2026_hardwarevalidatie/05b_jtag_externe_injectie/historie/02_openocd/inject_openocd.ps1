param(
  [Parameter(Mandatory=$true)][string]$Port,
  [Parameter(Mandatory=$true)][string]$AdapterSerial,
  [Parameter(Mandatory=$true)][string]$OutLog,
  [int]$Seconds = 18
)
. C:\Espressif\tools\Microsoft.v5.5.PowerShell_profile.ps1 *> $null
$ocdScripts = "C:\Espressif\tools\openocd-esp32\v0.12.0-esp32-20250422\openocd-esp32\share\openocd\scripts"
$jtagDir    = "D:\thesis\ClaudeCode\jtag"
$proj       = "D:\thesis\ClaudeCode\jtag\05b_jtag_lockstep"

# Symbooladressen (uit de elf)
$A_flip_invoer = "0x3fc94748"
$A_flip_res    = "0x3fc94744"
$A_flip_uit    = "0x3fc94740"
$A_det_base    = "0x3fc9472c"   # g_det_totaal, +4 uit, +8 res, +12 invoer

# 1) Instrument flashen (reset + run)
Write-Output "=== FLASH 05b instrument -> $Port ==="
Push-Location $proj
idf.py -p $Port flash 2>&1 | Select-String -Pattern 'Wrote|Hash of data verified|Hard reset|Error' | Select-Object -Last 4
Pop-Location
Start-Sleep -Seconds 2

# 2) Passieve seriele capture op de achtergrond
$cap = Start-Process -FilePath "python" -ArgumentList @("$jtagDir\passive_read.py", $Port, "$Seconds", $OutLog) -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 2

# 3) OpenOCD injectie-cfg genereren (geen reset: doelwit blijft draaien)
$injCfg = Join-Path $jtagDir ("inject_{0}.cfg" -f $Port)
@(
  "adapter serial $AdapterSerial"
  "source [find board/esp32s3-builtin.cfg]"
  "init"
  "echo {>> JTAG-injectie 1: INVOER  (mww g_flip_invoer 0x00000001)}"
  "halt"
  "mww $A_flip_invoer 0x00000001"
  "resume"
  "sleep 600"
  "echo {>> JTAG-injectie 2: VERWERKING (mww g_flip_res 0x00000040)}"
  "halt"
  "mww $A_flip_res 0x00000040"
  "resume"
  "sleep 600"
  "echo {>> JTAG-injectie 3: UITVOER (mww g_flip_uit 0x00000400)}"
  "halt"
  "mww $A_flip_uit 0x00000400"
  "resume"
  "sleep 600"
  "echo {>> Tellers teruglezen via JTAG (g_det_totaal, uit, res, invoer):}"
  "halt"
  "mdw $A_det_base 4"
  "resume"
  "shutdown"
) | Set-Content -Path $injCfg -Encoding ascii

Write-Output "=== OPENOCD JTAG-INJECTIE ($Port) ==="
& openocd -s $ocdScripts -f $injCfg 2>&1 |
    Select-String -Pattern 'JTAG-injectie|Tellers|0x3fc9472c|mdw|halt|Examination succeed|Error|error' |
    Select-Object -First 40

# 4) Wacht op capture
Wait-Process -Id $cap.Id -Timeout ($Seconds + 10) -ErrorAction SilentlyContinue

Write-Output "=== SERIELE DETECTIES ($Port) ==="
Get-Content $OutLog -ErrorAction SilentlyContinue |
    Select-String -Pattern 'GEDETECTEERD|hartslag|applicatiecore|checkercore|KLAAR-VOOR'
