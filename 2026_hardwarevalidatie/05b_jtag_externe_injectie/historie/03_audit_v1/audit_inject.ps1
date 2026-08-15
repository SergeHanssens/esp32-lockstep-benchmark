param(
  [Parameter(Mandatory=$true)][string]$Port,
  [Parameter(Mandatory=$true)][string]$AdapterSerial,
  [Parameter(Mandatory=$true)][string]$BoardName,
  [int]$Seconds = 16
)
# Volledige audittrail voor EEN JTAG-injectierun: OpenOCD-uitvoer (mww + readback
# + tellers) en seriele detecties samen in EEN gecombineerd log, met ELF/cfg-hash.
. C:\Espressif\tools\Microsoft.v5.5.PowerShell_profile.ps1 *> $null
$ocdScripts = "C:\Espressif\tools\openocd-esp32\v0.12.0-esp32-20250422\openocd-esp32\share\openocd\scripts"
$jtagDir = "D:\thesis\ClaudeCode\jtag"
$proj    = "D:\thesis\ClaudeCode\jtag\05b_jtag_lockstep"
$elf     = "$proj\build\lockstep_kern.elf"
$stamp   = Get-Date -Format "yyyy-MM-ddTHH:mm:ss"
$audit   = "D:\thesis\ClaudeCode\logs\audit_{0}.txt" -f ($BoardName -replace '[^A-Za-z0-9]','_')
$serTmp  = "$jtagDir\_ser_$Port.txt"
$ocdOut  = "$jtagDir\_ocd_$Port.txt"

# adressen (geverifieerd tegen ELF-symbolen)
$A = @{ det='0x3fc9472c'; flip_uit='0x3fc94740'; flip_res='0x3fc94744'; flip_invoer='0x3fc94748' }

# 1) instrument flashen -> tellers terug op 0
Push-Location $proj
idf.py -p $Port flash *> $null
Pop-Location
Start-Sleep -Seconds 2

# 2) passieve seriele capture
$cap = Start-Process -FilePath "python" -ArgumentList @("$jtagDir\passive_read.py",$Port,"$Seconds",$serTmp) -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 2

# 3) OpenOCD injectie MET readbacks
$cfg = "$jtagDir\audit_$Port.cfg"
# echo + Tcl read_memory: waarden komen zo GEGARANDEERD in het console-log
# (mdw/version worden naar het gdb-kanaal geleid zodra de gdb-server luistert).
@(
  "adapter serial $AdapterSerial"
  'source [find board/esp32s3-builtin.cfg]'
  'init'
  'echo "AUDIT OpenOCD [version]"'
  'echo "=== INJECTIE 1 INVOER (verwacht verdict 0x1) ==="'
  'halt'
  'echo "  pre  tellers totaal,uit,res,invoer = [read_memory 0x3fc9472c 32 4]"'
  'echo "  pre  g_flip_invoer                 = [read_memory 0x3fc94748 32 1]"'
  'mww 0x3fc94748 0x00000001'
  'echo "  mww  g_flip_invoer 0x1 -> readback = [read_memory 0x3fc94748 32 1]"'
  'resume'; 'sleep 700'
  'echo "=== INJECTIE 2 VERWERKING (verwacht verdict 0x2) ==="'
  'halt'
  'mww 0x3fc94744 0x00000040'
  'echo "  mww  g_flip_res 0x40 -> readback   = [read_memory 0x3fc94744 32 1]"'
  'resume'; 'sleep 700'
  'echo "=== INJECTIE 3 UITVOER (verwacht verdict 0x4) ==="'
  'halt'
  'mww 0x3fc94740 0x00000400'
  'echo "  mww  g_flip_uit 0x400 -> readback  = [read_memory 0x3fc94740 32 1]"'
  'resume'; 'sleep 700'
  'echo "=== EIND: tellerstand en triggers (moeten weer 0 zijn) ==="'
  'halt'
  'echo "  eind tellers totaal,uit,res,invoer = [read_memory 0x3fc9472c 32 4]"'
  'echo "  eind triggers uit,res,invoer       = [read_memory 0x3fc94740 32 3]"'
  'resume'
  'shutdown'
) | Set-Content -Path $cfg -Encoding ascii

# Start-Process met -RedirectStandardError vangt openocd-uitvoer schoon op
# (geen PowerShell NativeCommandError-wrapping die mdw-lijnen sloopt).
$p = Start-Process -FilePath "openocd" -ArgumentList @("-s",$ocdScripts,"-f",$cfg) `
     -RedirectStandardError $ocdOut -RedirectStandardOutput ($ocdOut + ".out") `
     -NoNewWindow -PassThru
Wait-Process -Id $p.Id -Timeout 40 -ErrorAction SilentlyContinue
Wait-Process -Id $cap.Id -Timeout ($Seconds + 10) -ErrorAction SilentlyContinue

# 4) hashes
$elfHash = (Get-FileHash $elf -Algorithm MD5).Hash
$cfgHash = (Get-FileHash $cfg -Algorithm MD5).Hash

# 5) gecombineerd audit-log opbouwen
$hdr = @(
  "================ JTAG-INJECTIE AUDITTRAIL ================"
  "bord           : $BoardName ($Port)"
  "adapter serial : $AdapterSerial"
  "tijd           : $stamp"
  "firmware ELF   : $elf"
  "ELF MD5        : $elfHash"
  "OpenOCD cfg MD5: $cfgHash"
  "adres-map      : g_det_totaal=$($A.det) g_flip_uit=$($A.flip_uit) g_flip_res=$($A.flip_res) g_flip_invoer=$($A.flip_invoer)"
  "verwacht       : injectie1->0x1[INVOER], injectie2->0x2[VERWERKING], injectie3->0x4[UITVOER]"
  "========================================================="
  ""
  "----- OpenOCD JTAG-transcript (mww + readback + tellers) -----"
)
$ocdBody = Get-Content $ocdOut -ErrorAction SilentlyContinue |
    Select-String -Pattern 'AUDIT OpenOCD|INJECTIE|EIND|readback|tellers|g_flip|Examination succeed' |
    ForEach-Object { $_.Line }
$serBody = @("", "----- Seriele detecties van het bord -----") +
    (Get-Content $serTmp -ErrorAction SilentlyContinue | Select-String -Pattern 'GEDETECTEERD|hartslag|applicatiecore|checkercore' | ForEach-Object { $_.Line })

($hdr + $ocdBody + $serBody) | Set-Content -Path $audit -Encoding utf8
Write-Output "AUDIT geschreven: $audit"
Write-Output "----------------------------------------------"
Get-Content $audit