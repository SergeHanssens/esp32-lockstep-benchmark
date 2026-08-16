<#
 meet_alle_borden.ps1 - orkestreert de meetcampagne over alle aangesloten
 ESP32-S3-borden voor de masterproef (softwarematige checkpoint-lockstep).

 Waarom deze wrapper bestaat
 ---------------------------
 De bestaande scripts (meet_coremark_runs.ps1, build_flash_capture_07.ps1)
 gaan uit van één bord op COM3 en schrijven in dagbestanden zonder bordkolom.
 Met negen borden levert dat een dataset op waarin niet meer te achterhalen is
 welk bord welk getal gaf. Deze wrapper lost drie dingen op:
   1. hij sleutelt op het USB-serienummer van de JTAG-adapter, niet op COM-nummer;
      COM-nummers verschuiven bij herenumeratie, serienummers niet;
   2. hij bouwt één keer per project en flasht daarna per bord, zodat alle
      borden aantoonbaar dezelfde binary draaien (SHA-256 van de .bin in het manifest);
   3. hij archiveert per bord: ruwe logs, hashes, manifest, buildinfo.

 Gebruik
 -------
   powershell -ExecutionPolicy Bypass -File .\meet_alle_borden.ps1 -WhatIf
   powershell -ExecutionPolicy Bypass -File .\meet_alle_borden.ps1 -Rondes 04
   powershell -ExecutionPolicy Bypass -File .\meet_alle_borden.ps1 -Rondes 04,07,jtag -Runs04 5 -Runs07 5
   powershell -ExecutionPolicy Bypass -File .\meet_alle_borden.ps1 -Rondes 07 -AlleenBorden DK1,F24A

 Er wordt niets gemeten dat niet ook ruw gelogd wordt. Bij een mislukking gaat
 de wrapper door naar het volgende bord en rapporteert op het einde.
#>

[CmdletBinding()]
param(
    [switch]   $WhatIf,
    [string]   $RepoMap     = 'D:\ClaudeMasterproef\thesis\esp32-lockstep-benchmark',
    # BasisMap = waar de ESP-IDF-projecten 04 en 07 staan. Standaard de repo,
    # zodat aantoonbaar de gepubliceerde bron gemeten wordt. Wil je toch de
    # werkkopie meten: -BasisMap 'D:\ClaudeMasterproef\thesis'
    [string]   $BasisMap    = '',
    [string]   $JtagMap     = 'D:\ClaudeMasterproef\thesis\ClaudeCode\jtag',
    [string]   $BordenCsv   = '',
    [string]   $MeetPy      = '',
    [string[]] $Rondes      = @('04', '07'),
    [ValidateRange(1,200)][int] $Runs04 = 5,
    [ValidateRange(1,200)][int] $Runs07 = 5,
    [int]      $JtagSeconden = 45,
    [string[]] $AlleenBorden = @(),
    [string]   $IdfProfiel  = 'C:\Espressif\tools\Microsoft.v5.5.PowerShell_profile.ps1',
    [string]   $Sessie      = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# $PSScriptRoot is leeg binnen param(): parameterstandaarden worden in de scope
# van de aanroeper geevalueerd. Daarom hier pas invullen.
$ScriptMap = $PSScriptRoot
if (-not $ScriptMap) { $ScriptMap = Split-Path -Parent $MyInvocation.MyCommand.Path }
if (-not $ScriptMap) { $ScriptMap = (Get-Location).Path }
if (-not $BordenCsv) { $BordenCsv = Join-Path $ScriptMap 'borden.csv' }
if (-not $MeetPy)    { $MeetPy    = Join-Path $ScriptMap 'meet_bord.py' }
if (-not $BasisMap)  { $BasisMap  = Join-Path $RepoMap '2026_hardwarevalidatie' }

# ---------------------------------------------------------------------------
# 0. sessiemap en logboek
# ---------------------------------------------------------------------------

if (-not $Sessie) { $Sessie = Get-Date -Format 'yyyyMMdd_HHmmss' }
$SessieMap = Join-Path $RepoMap ("2026_hardwarevalidatie\metingen\campagne_$Sessie")
$Verslag   = Join-Path $SessieMap 'verslag.txt'

function Schrijf {
    param([string]$Tekst, [string]$Kleur = 'Gray')
    $regel = "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $Tekst
    Write-Host $regel -ForegroundColor $Kleur
    if (Test-Path (Split-Path $Verslag)) { Add-Content -Path $Verslag -Value $regel -Encoding UTF8 }
}

# ---------------------------------------------------------------------------
# 1. bordinventaris: serienummer -> huidige COM-poort
# ---------------------------------------------------------------------------

function Get-BordInventaris {
    <# Leest de werkelijk aangesloten borden uit Windows. Retourneert objecten
       met Serie, Poort, PID, Jtag. Zelfde logica als borden_inventaris.ps1. #>
    $borden = Get-PnpDevice -PresentOnly -Class USB |
              Where-Object { $_.InstanceId -like 'USB\VID_303A&PID_*' -and $_.InstanceId -notlike '*MI_*' }

    # kinderen één keer ophalen in plaats van per bord: scheelt seconden
    $kinderen = @()
    foreach ($k in Get-PnpDevice -PresentOnly) {
        if ($k.InstanceId -notlike 'USB\VID_303A*MI_*') { continue }
        $par = $null
        try { $par = (Get-PnpDeviceProperty -InstanceId $k.InstanceId -KeyName 'DEVPKEY_Device_Parent' -WhatIf:$false -Confirm:$false).Data } catch { }
        if ($par) { $kinderen += [pscustomobject]@{ Parent = $par; Naam = $k.FriendlyName } }
    }

    foreach ($b in $borden) {
        $serie = ($b.InstanceId -split '\\')[-1]
        $pidHex = if ($b.InstanceId -match 'PID_([0-9A-Fa-f]{4})') { $matches[1] } else { '?' }
        $poort = $null; $jtag = $false
        foreach ($k in ($kinderen | Where-Object { $_.Parent -eq $b.InstanceId })) {
            if ($k.Naam -match '\((COM\d+)\)') { $poort = $matches[1] }
            if ($k.Naam -match 'JTAG')         { $jtag = $true }
        }
        [pscustomobject]@{ Serie = $serie; Poort = $poort; PID = $pidHex; Jtag = $jtag }
    }
}

# ---------------------------------------------------------------------------
# 2. bouwen en flashen
# ---------------------------------------------------------------------------

function Invoke-Bouw {
    param([string]$ProjectMap, [string]$Naam)
    Schrijf "bouwen: $Naam" 'Cyan'
    Push-Location $ProjectMap
    try {
        $log = Join-Path $SessieMap "build_$Naam.log"
        idf.py build *> $log
        if ($LASTEXITCODE -ne 0) {
            Schrijf "BOUW MISLUKT ($Naam), laatste regels:" 'Red'
            Get-Content $log -Tail 25 | ForEach-Object { Write-Host "    $_" }
            throw "bouw $Naam mislukt"
        }
    } finally { Pop-Location }

    # binary-hash vastleggen: bewijst dat elk bord dezelfde firmware kreeg.
    # Het buildsysteem noemt de applicatiebinary zelf in project_description.json;
    # dat is betrouwbaarder dan "de grootste .bin die geen bootloader heet".
    $bin = $null
    $desc = Join-Path $ProjectMap 'build\project_description.json'
    if (Test-Path $desc) {
        try {
            $d = Get-Content $desc -Raw | ConvertFrom-Json
            $kandidaat = Join-Path $ProjectMap ("build\" + $d.app_bin)
            if (Test-Path $kandidaat) { $bin = Get-Item $kandidaat }
        } catch { }
    }
    if (-not $bin) {
        $bin = Get-ChildItem (Join-Path $ProjectMap 'build') -Filter '*.bin' -File |
               Where-Object { $_.Name -notmatch 'bootloader|partition' } |
               Sort-Object Length -Descending | Select-Object -First 1
    }
    if (-not $bin) { throw "geen applicatie-.bin gevonden in $ProjectMap\build" }
    $hash = (Get-FileHash $bin.FullName -Algorithm SHA256).Hash
    Schrijf ("binary {0}  sha256 {1}" -f $bin.Name, $hash) 'DarkGray'
    return [pscustomobject]@{ Bin = $bin.FullName; Sha256 = $hash; Project = $ProjectMap }
}

function Invoke-Flash {
    param([string]$ProjectMap, [string]$Poort, [string]$Bordnaam, [string]$Naam,
          [string]$VerwachteBin, [string]$VerwachteHash)
    # bewijs dat elk bord dezelfde binary krijgt: idf.py flash draait het
    # buildsysteem opnieuw, dus de hash wordt voor elke flash hercontroleerd
    if ($VerwachteBin -and (Test-Path $VerwachteBin)) {
        $nu = (Get-FileHash $VerwachteBin -Algorithm SHA256).Hash
        if ($nu -ne $VerwachteHash) {
            Schrijf "BINARY GEWIJZIGD voor $Bordnaam : $nu (was $VerwachteHash)" 'Red'
            return $false
        }
    }
    Push-Location $ProjectMap
    try {
        $log = Join-Path $SessieMap "flash_${Naam}_${Bordnaam}.log"
        idf.py -p $Poort flash *> $log
        if ($LASTEXITCODE -ne 0) {
            Schrijf "FLASH MISLUKT ($Naam op $Bordnaam / $Poort):" 'Red'
            Get-Content $log -Tail 20 | ForEach-Object { Write-Host "    $_" }
            return $false
        }
    } finally { Pop-Location }

    # idf.py flash draait eerst het buildsysteem: pas NA de flash weet je zeker
    # dat de geflashte binary dezelfde is als de gehashte
    if ($VerwachteBin -and (Test-Path $VerwachteBin)) {
        $na = (Get-FileHash $VerwachteBin -Algorithm SHA256).Hash
        if ($na -ne $VerwachteHash) {
            Schrijf "BINARY HERBOUWD TIJDENS FLASH van $Bordnaam : $na (was $VerwachteHash)" 'Red'
            return $false
        }
    }
    Start-Sleep -Seconds 2      # poort vrijgeven voor de capture
    return $true
}

function Resolve-Bord {
    <# Lost het serienummer opnieuw op naar de actuele COM-poort. COM-nummers
       kunnen tijdens een campagne van uren verspringen; zonder deze
       herresolutie zou een vastgezette poort naar een ander fysiek bord kunnen
       wijzen en zou de meting stil onder de verkeerde naam terechtkomen.
       Retourneert $null als het bord niet meer bruikbaar aanwezig is. #>
    param([string]$Serie, [int]$Deadline = 10)
    $einde = (Get-Date).AddSeconds($Deadline)
    do {
        $kandidaten = @(Get-BordInventaris | Where-Object { $_.Serie -eq $Serie })
        if ($kandidaten.Count -eq 1) {
            $k = $kandidaten[0]
            if ($k.Poort -and $k.PID -eq '1001' -and $k.Jtag) { return $k }
        } elseif ($kandidaten.Count -gt 1) {
            Schrijf "serienummer $Serie komt $($kandidaten.Count)x voor: onbruikbaar" 'Red'
            return $null
        }
        Start-Sleep -Seconds 2
    } while ((Get-Date) -lt $einde)
    return $null
}

function Stop-PoortBlokkers {
    Get-CimInstance Win32_Process |
        Where-Object { $_.CommandLine -match 'idf_monitor|idf\.py monitor|openocd' } |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Seconds 3
}

# ---------------------------------------------------------------------------
# 3. voorbereiding
# ---------------------------------------------------------------------------

if (-not (Test-Path $BordenCsv)) { throw "bordtabel niet gevonden: $BordenCsv" }
if (-not (Test-Path $MeetPy))    { throw "meetscript niet gevonden: $MeetPy" }

$tabel = Import-Csv $BordenCsv
if ($AlleenBorden.Count -gt 0) {
    $tabel = $tabel | Where-Object { $AlleenBorden -contains $_.bordnaam }
}
if (-not $tabel) { throw "geen borden geselecteerd" }
foreach ($kolom in @('serie','bordtype','bordnaam')) {
    if ($tabel[0].PSObject.Properties.Name -notcontains $kolom) {
        throw "kolom '$kolom' ontbreekt in $BordenCsv (BOM of verkeerd scheidingsteken?)"
    }
}
$dubbel = @($tabel | Group-Object serie | Where-Object Count -gt 1)
if ($dubbel.Count -gt 0) { throw ("dubbele serienummers in bordtabel: " + ($dubbel.Name -join ', ')) }

Write-Host ''
Write-Host '=== bordinventaris uitlezen ===' -ForegroundColor Cyan
$inv = @(Get-BordInventaris)

$plan = foreach ($r in $tabel) {
    $gevonden = $inv | Where-Object { $_.Serie -eq $r.serie } | Select-Object -First 1
    [pscustomobject]@{
        Bordnaam = $r.bordnaam
        Bordtype = $r.bordtype
        Serie    = $r.serie
        Poort    = if ($gevonden) { $gevonden.Poort } else { $null }
        PID      = if ($gevonden) { $gevonden.PID }   else { '-' }
        Jtag     = if ($gevonden) { $gevonden.Jtag }  else { $false }
        Klaar    = [bool]($gevonden -and $gevonden.Poort -and $gevonden.PID -eq '1001' -and $gevonden.Jtag)
    }
}

$plan | Format-Table Bordnaam, Bordtype, Serie, Poort, PID, Jtag, Klaar -AutoSize |
        Out-String -Width 140 | Write-Host

$nietKlaar = @($plan | Where-Object { -not $_.Klaar })
if ($nietKlaar.Count -gt 0) {
    Write-Host "Niet klaar: $($nietKlaar.Bordnaam -join ', ')" -ForegroundColor Yellow
    Write-Host "Oorzaak is meestal dat de draaiende firmware een eigen USB-apparaat opzet." -ForegroundColor Yellow
    Write-Host "Oplossing: Restart to bootloader (of BOOT vasthouden + RESET) en opnieuw." -ForegroundColor Yellow
}
$werk = @($plan | Where-Object { $_.Klaar })
if ($werk.Count -eq 0) { throw "geen enkel bord klaar" }

# ruwe tijdsraming zodat je weet waar je aan begint
$sec = 0
if ($Rondes -contains '04')   { $sec += $werk.Count * (60 + $Runs04 * 95) }
if ($Rondes -contains '07')   { $sec += $werk.Count * (90 + $Runs07 * 260) }
if ($Rondes -contains 'jtag') { $sec += $werk.Count * (90 + $JtagSeconden + 30) }
Write-Host ("Rondes: {0} | borden: {1} | ruwe raming: {2:hh\:mm} (boven-grens, geen belofte)" -f `
    ($Rondes -join ','), $werk.Count, [TimeSpan]::FromSeconds($sec)) -ForegroundColor Cyan
Write-Host ''

if ($WhatIf) {
    Write-Host 'Droogloop: hier stopt het. Bovenstaande tabel is de enige uitvoer.' -ForegroundColor Yellow
    return
}

New-Item -ItemType Directory -Force -Path $SessieMap | Out-Null
Schrijf "sessiemap: $SessieMap" 'Cyan'
Schrijf ("rondes: {0} | borden: {1}" -f ($Rondes -join ','), ($werk.Bordnaam -join ',')) 'Cyan'

if (Test-Path $IdfProfiel) { . $IdfProfiel | Out-Null; Schrijf "ESP-IDF-profiel geladen" }
else { Schrijf "ESP-IDF-profiel niet gevonden op $IdfProfiel - idf.py moet al in PATH staan" 'Yellow' }

# omgeving vastleggen: hoort bij de bewijsketen
$omgeving = [ordered]@{
    sessie      = $Sessie
    tijd_utc    = (Get-Date).ToUniversalTime().ToString('s') + 'Z'
    computer    = $env:COMPUTERNAME
    gebruiker   = $env:USERNAME
    basismap    = $BasisMap
    repomap     = $RepoMap
    rondes      = $Rondes
    runs04      = $Runs04
    runs07      = $Runs07
    idf_versie  = (& idf.py --version 2>&1 | Out-String).Trim()
    python      = (& python --version 2>&1 | Out-String).Trim()
    git_commit  = (& git -C $RepoMap rev-parse HEAD 2>&1 | Out-String).Trim()
    git_schoon  = ((& git -C $RepoMap status --porcelain 2>&1 | Out-String).Trim() -eq '')
    borden      = $plan
}

# ---------------------------------------------------------------------------
# 4. de rondes
# ---------------------------------------------------------------------------

$resultaten = @()
$builds = @{}

foreach ($ronde in $Rondes) {

    if ($ronde -eq 'jtag') {
        # ---- ronde JTAG: externe, debugger-getriggerde foutinjectie -------
        $auditScript = Join-Path $JtagMap 'audit_inject_v3.ps1'
        if (-not (Test-Path $auditScript)) {
            Schrijf "auditscript niet gevonden: $auditScript - ronde jtag overgeslagen" 'Red'
            continue
        }
        foreach ($b in $werk) {
            $ok = $false; $reden = ''
            try {
                $live = Resolve-Bord -Serie $b.Serie -Deadline 15
                if (-not $live) { throw "bord niet bruikbaar aanwezig (serie $($b.Serie))" }
                $b.Poort = $live.Poort
                Schrijf "JTAG-audit op $($b.Bordnaam) ($($b.Poort))" 'Cyan'
                Stop-PoortBlokkers

                # apart proces: een 'exit' in het auditscript zou anders deze
                # hele campagne beeindigen, en zijn exitcode is hier isoleerbaar
                $proc = Start-Process -FilePath 'powershell.exe' -Wait -NoNewWindow -PassThru `
                    -ArgumentList @('-ExecutionPolicy','Bypass','-File', $auditScript,
                                    '-Port', $b.Poort, '-AdapterSerial', $b.Serie,
                                    '-BoardName', $b.Bordnaam, '-Seconds', "$JtagSeconden")
                $ok = ($proc.ExitCode -eq 0)
                if (-not $ok) { $reden = "auditscript exitcode $($proc.ExitCode)" }
            } catch {
                $reden = "$_"
                Schrijf "JTAG-audit fout op $($b.Bordnaam): $_" 'Red'
            } finally {
                $resultaten += [pscustomobject]@{ Bord = $b.Bordnaam; Ronde = 'jtag'
                                                  Ok = $ok; Reden = $reden }
            }
        }
        continue
    }

    # ---- rondes 04 en 07: bouwen, flashen, meten ------------------------
    $projectMap = switch ($ronde) {
        '04' { Join-Path $BasisMap '04_coremark' }
        '07' { Join-Path $BasisMap '07_coremark_lockstep' }
    }
    if (-not (Test-Path $projectMap)) {
        Schrijf "projectmap niet gevonden: $projectMap - ronde $ronde overgeslagen" 'Red'
        continue
    }

    Schrijf "projectmap ronde $ronde : $projectMap" 'DarkGray'
    try {
        $builds[$ronde] = Invoke-Bouw -ProjectMap $projectMap -Naam $ronde
    } catch {
        Schrijf "bouw $ronde mislukt: $_ - ronde overgeslagen, campagne gaat door" 'Red'
        $resultaten += [pscustomobject]@{ Bord = '(alle)'; Ronde = $ronde; Ok = $false
                                          Reden = "bouw mislukt: $_" }
        continue
    }
    $runs = if ($ronde -eq '04') { $Runs04 } else { $Runs07 }

    foreach ($b in $werk) {
        $reden = ''
        $ok = $false
        try {
            # poort opnieuw oplossen op serienummer: COM-nummers kunnen verspringen
            $live = Resolve-Bord -Serie $b.Serie -Deadline 15
            if (-not $live) {
                throw "bord niet meer bruikbaar aanwezig (serie $($b.Serie))"
            }
            if ($live.Poort -ne $b.Poort) {
                Schrijf "$($b.Bordnaam): poort verschoven van $($b.Poort) naar $($live.Poort)" 'Yellow'
                $b.Poort = $live.Poort
            }

            Schrijf "ronde $ronde op $($b.Bordnaam) ($($b.Poort), $($b.Bordtype))" 'Cyan'
            Stop-PoortBlokkers

            if (-not (Invoke-Flash -ProjectMap $projectMap -Poort $b.Poort `
                                   -Bordnaam $b.Bordnaam -Naam $ronde `
                                   -VerwachteBin $builds[$ronde].Bin `
                                   -VerwachteHash $builds[$ronde].Sha256)) {
                throw 'flash mislukt'
            }

            # het bord herstart na de flash en enumereert opnieuw: poort kan
            # tussen flash en capture verspringen
            $live = Resolve-Bord -Serie $b.Serie -Deadline 25
            if (-not $live) { throw "bord na flash niet teruggevonden (serie $($b.Serie))" }
            if ($live.Poort -ne $b.Poort) {
                Schrijf "$($b.Bordnaam): poort na flash $($b.Poort) -> $($live.Poort)" 'Yellow'
                $b.Poort = $live.Poort
            }

            & python $MeetPy --soort $ronde --poort $b.Poort --serie $b.Serie `
                     --bordtype $b.Bordtype --bordnaam $b.Bordnaam `
                     --runs $runs --uitmap $SessieMap --stil-sec 120
            switch ($LASTEXITCODE) {
                0       { $ok = $true }
                2       { $ok = $false; $reden = 'niet alle runs geldig' }
                3       { $ok = $false; $reden = 'meting niet gestart (map bestaat al)' }
                default { $ok = $false; $reden = 'geen enkele geldige run' }
            }
            if (-not $ok) { Schrijf "ronde $ronde op $($b.Bordnaam): $reden" 'Red' }
        } catch {
            $reden = "$_"
            $ok = $false
            Schrijf "ronde $ronde op $($b.Bordnaam) afgebroken: $_" 'Red'
        } finally {
            $resultaten += [pscustomobject]@{
                Bord = $b.Bordnaam; Ronde = $ronde; Ok = $ok; Reden = $reden
            }
            # tussentijds wegschrijven: bij een crash op bord 7 blijft het
            # resultaat van bord 1 t/m 6 bewaard
            $tmp = Join-Path $SessieMap 'resultaten_tussentijds.json.tmp'
            $def = Join-Path $SessieMap 'resultaten_tussentijds.json'
            [IO.File]::WriteAllText($tmp, ($resultaten | ConvertTo-Json -Depth 4),
                                    (New-Object Text.UTF8Encoding($false)))
            Move-Item -Path $tmp -Destination $def -Force
        }
    }
}

# ---------------------------------------------------------------------------
# 5. afsluiten: manifest en samenvatting
# ---------------------------------------------------------------------------

$omgeving['builds'] = $builds
$omgeving['resultaten'] = $resultaten
[IO.File]::WriteAllText((Join-Path $SessieMap 'campagne_manifest.json'),
                        ($omgeving | ConvertTo-Json -Depth 6),
                        (New-Object Text.UTF8Encoding($false)))

Write-Host ''
Write-Host '=== samenvatting ===' -ForegroundColor Cyan
$resultaten | Format-Table Bord, Ronde, Ok, Reden -AutoSize | Out-String -Width 120 | Write-Host

$fout = @($resultaten | Where-Object { -not $_.Ok })
Schrijf ("klaar: {0} van {1} bord-rondes geslaagd" -f ($resultaten.Count - $fout.Count), $resultaten.Count) `
        $(if ($fout.Count -eq 0) { 'Green' } else { 'Yellow' })
Schrijf "alles staat in: $SessieMap" 'Cyan'
