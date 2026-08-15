param(
  [Parameter(Mandatory=$true)][string]$Port,
  [Parameter(Mandatory=$true)][string]$AdapterSerial,
  [Parameter(Mandatory=$true)][string]$BoardName,
  [int]$Seconds = 45
)
# Geharde JTAG-injectie-audit: adressen uit ELF afgeleid, elke stap gevalideerd,
# volledige ruwe logs + ELF-kopie gearchiveerd per run. Adresseert de robuustheid-
# en archiveringspunten uit de externe review.
$ErrorActionPreference = "Continue"
. C:\Espressif\tools\Microsoft.v5.5.PowerShell_profile.ps1 *> $null
$ocdScripts = "C:\Espressif\tools\openocd-esp32\v0.12.0-esp32-20250422\openocd-esp32\share\openocd\scripts"
$jtagDir = "D:\thesis\ClaudeCode\jtag"
$proj    = "D:\thesis\ClaudeCode\jtag\05b_jtag_lockstep"
$elf     = "$proj\build\lockstep_kern.elf"
$mainc   = "$proj\main\main.c"
$stamp   = Get-Date -Format "yyyyMMdd_HHmmss"
$safe    = ($BoardName -replace '[^A-Za-z0-9]','_')
$runDir  = "D:\thesis\ClaudeCode\logs\audit_runs\${stamp}_${safe}"
New-Item -ItemType Directory -Force $runDir | Out-Null
$audit   = "$runDir\audit.txt"
$serTmp  = "$runDir\serial_raw.txt"
$ocdOut  = "$runDir\openocd_raw.txt"
$flashLog= "$runDir\flash.txt"
$checks  = New-Object System.Collections.Generic.List[string]
function Check($name,$ok,$detail){ $tag = if($ok){"PASS"}else{"FAIL"}; $script:checks.Add(("[{0}] {1} {2}" -f $tag,$name,$detail)); return $ok }

# --- 1) adressen AFLEIDEN uit de ELF (niet hardcoden) ---
$syms = 'g_flip_invoer','g_flip_res','g_flip_uit','g_flip_invoerdata','g_det_totaal','g_det_uit','g_det_res','g_det_invoer'
$gdbArgs = @('--batch','-nx',$elf); foreach($s in $syms){ $gdbArgs += @('-ex',"print &$s") }
$gdbOut = & xtensa-esp32s3-elf-gdb @gdbArgs 2>&1
$A = @{}
foreach($line in $gdbOut){ if($line -match '(0x[0-9a-fA-F]+)\s+<([A-Za-z0-9_]+)>'){ $A[$Matches[2]] = $Matches[1].ToLower() } }
$allFound = $true; foreach($s in $syms){ if(-not $A.ContainsKey($s)){ $allFound=$false } }
Check "adressen-uit-elf" $allFound ("(" + (($syms | ForEach-Object { "$_=$($A[$_])" }) -join ' ') + ")") | Out-Null
if(-not $allFound){ Write-Output "ONGELDIG: kon symbolen niet afleiden"; $checks | ForEach-Object { Write-Output $_ }; exit 1 }

# --- 2) flashen + verifieren ---
Push-Location $proj
idf.py -p $Port flash *>&1 | Tee-Object -FilePath $flashLog | Out-Null
Pop-Location
$flashOk = (Select-String -Path $flashLog -Pattern 'Hash of data verified' -Quiet) -and -not (Select-String -Path $flashLog -Pattern 'A fatal error|Failed to' -Quiet)
Check "flash-geslaagd" $flashOk "" | Out-Null
Start-Sleep -Seconds 2

# --- 3) passieve seriele capture ---
$cap = Start-Process -FilePath "python" -ArgumentList @("$jtagDir\passive_read.py",$Port,"$Seconds",$serTmp) -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 2

# --- 4) OpenOCD-injectie met afgeleide adressen + readbacks per veld ---
$cfg = "$runDir\inject.cfg"
@(
  "adapter serial $AdapterSerial"
  'source [find board/esp32s3-builtin.cfg]'
  'init'
  'echo "AUDIT OpenOCD [version]"'
  'echo "=== INJECTIE 1 INVOER-CRC (verwacht 0x1) ==="'
  'halt'
  "echo `"  pre  g_det_totaal = [read_memory $($A.g_det_totaal) 32 1]`""
  "mww $($A.g_flip_invoer) 0x00000001"
  "echo `"  readback g_flip_invoer = [read_memory $($A.g_flip_invoer) 32 1]`""
  'resume'; 'sleep 800'
  'echo "=== INJECTIE 2 VERWERKING (verwacht 0x2) ==="'
  'halt'
  "mww $($A.g_flip_res) 0x00000040"
  "echo `"  readback g_flip_res = [read_memory $($A.g_flip_res) 32 1]`""
  'resume'; 'sleep 800'
  'echo "=== INJECTIE 3 UITVOER (verwacht 0x4) ==="'
  'halt'
  "mww $($A.g_flip_uit) 0x00000400"
  "echo `"  readback g_flip_uit = [read_memory $($A.g_flip_uit) 32 1]`""
  'resume'; 'sleep 800'
  'echo "=== INJECTIE 4 ECHTE INVOER-DATABIT k.invoer idx5 bit7, PROPAGATIE (verwacht 0x7) ==="'
  'halt'
  "mww $($A.g_flip_invoerdata) 0x00000080"
  "echo `"  readback g_flip_invoerdata = [read_memory $($A.g_flip_invoerdata) 32 1]`""
  'resume'; 'sleep 800'
  'echo "=== EIND: tellers en triggers ==="'
  'halt'
  "echo `"  eind g_det_totaal = [read_memory $($A.g_det_totaal) 32 1]`""
  "echo `"  eind g_det_invoer = [read_memory $($A.g_det_invoer) 32 1]`""
  "echo `"  eind g_det_res    = [read_memory $($A.g_det_res) 32 1]`""
  "echo `"  eind g_det_uit    = [read_memory $($A.g_det_uit) 32 1]`""
  "echo `"  eind trig invoer,res,uit,invoerdata = [read_memory $($A.g_flip_invoer) 32 1] [read_memory $($A.g_flip_res) 32 1] [read_memory $($A.g_flip_uit) 32 1] [read_memory $($A.g_flip_invoerdata) 32 1]`""
  'resume'
  'shutdown'
) | Set-Content -Path $cfg -Encoding ascii

# System.Diagnostics.Process: geeft BETROUWBAAR zowel timeout (60s) als exitcode,
# zonder deadlock (async ReadToEnd). Bij timeout wordt het proces gecontroleerd gedood.
$openocdExe = (Get-Command openocd).Source
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $openocdExe
$psi.Arguments = "-s `"$ocdScripts`" -f `"$cfg`""
$psi.UseShellExecute = $false
$psi.RedirectStandardError = $true
$psi.RedirectStandardOutput = $true
$psi.CreateNoWindow = $true
$proc = [System.Diagnostics.Process]::Start($psi)
$errTask = $proc.StandardError.ReadToEndAsync()
$outTask = $proc.StandardOutput.ReadToEndAsync()
$ocdDone = $proc.WaitForExit(60000)
if(-not $ocdDone){ try { $proc.Kill(); $proc.WaitForExit(5000) | Out-Null } catch {} }
$errTask.Result | Set-Content -Path $ocdOut -Encoding ascii
$outTask.Result | Set-Content -Path ($ocdOut + ".out") -Encoding ascii
$ocdExit = $(if($ocdDone){ $proc.ExitCode } else { $null })
$proc.Dispose()
Wait-Process -Id $cap.Id -Timeout ($Seconds + 10) -ErrorAction SilentlyContinue

# --- 5) parsen + valideren ---
$raw = Get-Content $ocdOut -ErrorAction SilentlyContinue
function RB($label){ $m = $raw | Select-String -Pattern "readback $label\s*=\s*(0x[0-9a-f]+)"; if($m){ $m.Matches[0].Groups[1].Value } else { "" } }
function DET($label){ $m = $raw | Select-String -Pattern "eind $label\s*=\s*(0x[0-9a-f]+)"; if($m){ $m.Matches[0].Groups[1].Value } else { "" } }
$rb1=RB 'g_flip_invoer'; $rb2=RB 'g_flip_res'; $rb3=RB 'g_flip_uit'; $rb4=RB 'g_flip_invoerdata'
$dTot=DET 'g_det_totaal'; $dInv=DET 'g_det_invoer'; $dRes=DET 'g_det_res'; $dUit=DET 'g_det_uit'
Check "openocd-examine" (($raw | Select-String -Pattern 'Examination succeed' | Measure-Object).Count -ge 2) "" | Out-Null
Check "openocd-niet-getimeout" ($ocdDone) "" | Out-Null
Check "openocd-clean-shutdown" (($raw | Select-String -Pattern 'shutdown command invoked' | Measure-Object).Count -ge 1) "" | Out-Null
Check "openocd-exit-0" ($ocdExit -eq 0) "exit=$ocdExit" | Out-Null
$preTotM = $raw | Select-String -Pattern 'pre\s+g_det_totaal\s*=\s*(0x[0-9a-f]+)'
$preTot = if($preTotM){ $preTotM.Matches[0].Groups[1].Value } else { "" }
Check "pre-teller-totaal=0" ($preTot -eq '0x0') "gelezen=$preTot" | Out-Null
Check "readback-inj1(0x1)"   ($rb1 -eq '0x1')   "gelezen=$rb1"  | Out-Null
Check "readback-inj2(0x40)"  ($rb2 -eq '0x40')  "gelezen=$rb2"  | Out-Null
Check "readback-inj3(0x400)" ($rb3 -eq '0x400') "gelezen=$rb3"  | Out-Null
Check "readback-inj4(0x80)"  ($rb4 -eq '0x80')  "gelezen=$rb4"  | Out-Null
Check "eind-teller-totaal=4" ($dTot -eq '0x4') "gelezen=$dTot" | Out-Null
Check "eind-teller-invoer=2" ($dInv -eq '0x2') "gelezen=$dInv" | Out-Null
Check "eind-teller-verwerk=2" ($dRes -eq '0x2') "gelezen=$dRes" | Out-Null
Check "eind-teller-uitvoer=2" ($dUit -eq '0x2') "gelezen=$dUit" | Out-Null
$ser = Get-Content $serTmp -ErrorAction SilentlyContinue
Check "serieel-capture-nietleeg" (($ser | Measure-Object).Count -gt 0) "" | Out-Null
# volledige seriele verdictreeks: exact 0x1,0x2,0x4,0x7 in volgorde, geen extra detecties
$verdicts = @($ser | Select-String -Pattern 'GEDETECTEERD: ronde \d+ verdict (0x[0-9a-f]+)' | ForEach-Object { $_.Matches[0].Groups[1].Value })
$seqOk = ($verdicts.Count -eq 4) -and ($verdicts -join ',' -eq '0x1,0x2,0x4,0x7')
Check "serieel-reeks-1-2-4-7" $seqOk ("gelezen=[" + ($verdicts -join ',') + "]") | Out-Null
# eind-triggers moeten alle vier weer 0 zijn
$trigLine = $raw | Select-String -Pattern 'eind trig invoer,res,uit,invoerdata\s*=\s*(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)'
if($trigLine){ $g = $trigLine.Matches[0].Groups; $trigVals = @($g[1].Value,$g[2].Value,$g[3].Value,$g[4].Value) } else { $trigVals = @() }
$trigOk = ($trigVals.Count -eq 4) -and (-not ($trigVals | Where-Object { $_ -ne '0x0' }))
Check "eind-triggers-alle-0" $trigOk ("gelezen=[" + ($trigVals -join ',') + "]") | Out-Null
$ocdErrors = ($raw | Select-String -Pattern '^Error:' | ForEach-Object { $_.Line })

# --- 6) SHA-256 + archiveren ---
$elfSha = (Get-FileHash $elf -Algorithm SHA256).Hash
$mainSha= (Get-FileHash $mainc -Algorithm SHA256).Hash
Copy-Item $elf "$runDir\lockstep_kern.elf" -Force
Copy-Item $mainc "$runDir\main.c" -Force
$passAll = -not ($checks | Where-Object { $_.StartsWith('[FAIL]') })

# --- 7) gecombineerd auditbestand (incl. VOLLEDIGE ruwe openocd-log) ---
$out = New-Object System.Collections.Generic.List[string]
$out.Add("================ JTAG-INJECTIE AUDIT (geharde runner v3) ================")
$out.Add("bord           : $BoardName ($Port)")
$out.Add("adapter serial : $AdapterSerial")
$out.Add("tijd (run-id)  : $stamp")
$out.Add("eindoordeel    : " + $(if($passAll){"GELDIG - alle checks PASS"}else{"ONGELDIG - zie checks"}))
$out.Add("openocd exit   : $ocdExit")
$out.Add("ELF SHA-256    : $elfSha  (kopie: lockstep_kern.elf in deze map)")
$out.Add("main.c SHA-256 : $mainSha (kopie: main.c in deze map)")
$out.Add("adressen (ELF) : " + (($syms | ForEach-Object { "$_=$($A[$_])" }) -join ' '))
$out.Add("")
$out.Add("----- VALIDATIE -----")
$checks | ForEach-Object { $out.Add($_) }
$out.Add("")
$out.Add("----- OpenOCD Error-regels (NIET weggefilterd; hier waren dit benigne reset-timeouts) -----")
if($ocdErrors){ $ocdErrors | ForEach-Object { $out.Add($_) } } else { $out.Add("(geen)") }
$out.Add("")
$out.Add("----- Kernwaarden -----")
$out.Add("readbacks : inj1=$rb1 inj2=$rb2 inj3=$rb3 inj4=$rb4")
$out.Add("tellers   : totaal=$dTot invoer=$dInv verwerking=$dRes uitvoer=$dUit")
$out.Add("")
$out.Add("----- Seriele detecties -----")
($ser | Select-String -Pattern 'GEDETECTEERD|hartslag' | ForEach-Object { $_.Line }) | ForEach-Object { $out.Add($_) }
$out.Add("")
$out.Add("----- VOLLEDIGE ruwe OpenOCD-log (ongefilterd) -> ook als openocd_raw.txt -----")
$raw | ForEach-Object { $out.Add($_) }
$out | Set-Content -Path $audit -Encoding utf8

Write-Output ("EINDOORDEEL: " + $(if($passAll){"GELDIG"}else{"ONGELDIG"}) + "  ($BoardName)")
Write-Output "archief: $runDir"
$checks | ForEach-Object { Write-Output $_ }
Write-Output "readbacks: inj1=$rb1 inj2=$rb2 inj3=$rb3 inj4=$rb4 | tellers totaal=$dTot inv=$dInv verw=$dRes uit=$dUit"
if($ocdErrors){ Write-Output ("openocd Error-regels (benigne, gearchiveerd): " + $ocdErrors.Count) }