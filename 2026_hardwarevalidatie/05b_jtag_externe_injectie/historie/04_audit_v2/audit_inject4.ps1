param(
  [Parameter(Mandatory=$true)][string]$Port,
  [Parameter(Mandatory=$true)][string]$AdapterSerial,
  [Parameter(Mandatory=$true)][string]$BoardName,
  [int]$Seconds = 40
)
# Audittrail met VIER injecties: 3 geisoleerde single-checkpoint tests (0x1/0x2/0x4)
# + 1 ECHTE invoer-databitflip in k.invoer[5] die propageert -> verdict 0x7.
. C:\Espressif\tools\Microsoft.v5.5.PowerShell_profile.ps1 *> $null
$ocdScripts = "C:\Espressif\tools\openocd-esp32\v0.12.0-esp32-20250422\openocd-esp32\share\openocd\scripts"
$jtagDir = "D:\thesis\ClaudeCode\jtag"
$proj    = "D:\thesis\ClaudeCode\jtag\05b_jtag_lockstep"
$elf     = "$proj\build\lockstep_kern.elf"
$stamp   = Get-Date -Format "yyyy-MM-ddTHH:mm:ss"
$audit   = "D:\thesis\ClaudeCode\logs\audit4_{0}.txt" -f ($BoardName -replace '[^A-Za-z0-9]','_')
$serTmp  = "$jtagDir\_ser4_$Port.txt"
$ocdOut  = "$jtagDir\_ocd4_$Port.txt"

Push-Location $proj
idf.py -p $Port flash *> $null
Pop-Location
Start-Sleep -Seconds 2

$cap = Start-Process -FilePath "python" -ArgumentList @("$jtagDir\passive_read.py",$Port,"$Seconds",$serTmp) -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 2

$cfg = "$jtagDir\audit4_$Port.cfg"
@(
  "adapter serial $AdapterSerial"
  'source [find board/esp32s3-builtin.cfg]'
  'init'
  'echo "AUDIT OpenOCD [version]"'
  'echo "=== INJECTIE 1 INVOER-CRC (alleen CRC, verwacht 0x1) ==="'
  'halt'
  'echo "  pre  tellers totaal,uit,res,invoer = [read_memory 0x3fc9473c 32 4]"'
  'mww 0x3fc9475c 0x00000001'
  'echo "  mww  g_flip_invoer 0x1 -> readback = [read_memory 0x3fc9475c 32 1]"'
  'resume'; 'sleep 800'
  'echo "=== INJECTIE 2 VERWERKING (verwacht 0x2) ==="'
  'halt'
  'mww 0x3fc94758 0x00000040'
  'echo "  mww  g_flip_res 0x40 -> readback   = [read_memory 0x3fc94758 32 1]"'
  'resume'; 'sleep 800'
  'echo "=== INJECTIE 3 UITVOER (verwacht 0x4) ==="'
  'halt'
  'mww 0x3fc94754 0x00000400'
  'echo "  mww  g_flip_uit 0x400 -> readback  = [read_memory 0x3fc94754 32 1]"'
  'resume'; 'sleep 800'
  'echo "=== INJECTIE 4 ECHTE INVOER-DATABIT k.invoer idx5 bit7, PROPAGATIE (verwacht 0x7) ==="'
  'halt'
  'mww 0x3fc94750 0x00000080'
  'echo "  mww  g_flip_invoerdata 0x80 -> readback = [read_memory 0x3fc94750 32 1]"'
  'resume'; 'sleep 800'
  'echo "=== EIND: tellers en triggers (triggers moeten weer 0 zijn) ==="'
  'halt'
  'echo "  eind tellers totaal,uit,res,invoer      = [read_memory 0x3fc9473c 32 4]"'
  'echo "  eind triggers invoerdata,uit,res,invoer = [read_memory 0x3fc94750 32 4]"'
  'resume'
  'shutdown'
) | Set-Content -Path $cfg -Encoding ascii

$p = Start-Process -FilePath "openocd" -ArgumentList @("-s",$ocdScripts,"-f",$cfg) `
     -RedirectStandardError $ocdOut -RedirectStandardOutput ($ocdOut + ".out") -NoNewWindow -PassThru
Wait-Process -Id $p.Id -Timeout 40 -ErrorAction SilentlyContinue
Wait-Process -Id $cap.Id -Timeout ($Seconds + 10) -ErrorAction SilentlyContinue

$elfHash = (Get-FileHash $elf -Algorithm MD5).Hash
$cfgHash = (Get-FileHash $cfg -Algorithm MD5).Hash
$hdr = @(
  "================ JTAG-INJECTIE AUDITTRAIL (4 injecties) ================"
  "bord           : $BoardName ($Port)"
  "adapter serial : $AdapterSerial"
  "tijd           : $stamp"
  "firmware ELF   : $elf"
  "ELF MD5        : $elfHash"
  "OpenOCD cfg MD5: $cfgHash"
  "adres-map trig : g_flip_invoerdata=0x3fc94750 g_flip_uit=0x3fc94754 g_flip_res=0x3fc94758 g_flip_invoer=0x3fc9475c"
  "adres-map tel  : g_det_totaal=0x3fc9473c g_det_uit=0x3fc94740 g_det_res=0x3fc94744 g_det_invoer=0x3fc94748"
  "verwacht       : 1->0x1[INVOER], 2->0x2[VERWERKING], 3->0x4[UITVOER], 4->0x7[INVOER+VERWERKING+UITVOER]"
  "verwacht eind  : tellers totaal=4, uit=2, res=2, invoer=2"
  "======================================================================="
  ""
  "----- OpenOCD JTAG-transcript (mww + readback + tellers) -----"
)
$ocdBody = Get-Content $ocdOut -ErrorAction SilentlyContinue |
    Select-String -Pattern 'AUDIT OpenOCD|INJECTIE|EIND|readback|tellers|Examination succeed' |
    ForEach-Object { $_.Line }
$serBody = @("", "----- Seriele detecties van het bord -----") +
    (Get-Content $serTmp -ErrorAction SilentlyContinue | Select-String -Pattern 'GEDETECTEERD|hartslag|applicatiecore|checkercore' | ForEach-Object { $_.Line })
($hdr + $ocdBody + $serBody) | Set-Content -Path $audit -Encoding utf8
Write-Output "AUDIT geschreven: $audit"
Write-Output "----------------------------------------------"
Get-Content $audit