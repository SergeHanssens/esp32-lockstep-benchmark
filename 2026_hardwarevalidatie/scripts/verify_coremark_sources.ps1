# Verifieert dat de CoreMark-kernbestanden in 04_coremark/main byte-identiek
# (na LF-normalisatie) zijn aan EEMBC upstream commit 1f483d5.
# Gebruik: powershell -File verify_coremark_sources.ps1 [-BronMap <pad>]
param(
    [string]$BronMap = (Join-Path $PSScriptRoot '..\04_coremark\main'),
    [string]$RefBestand = (Join-Path $PSScriptRoot 'coremark_md5_upstream_1f483d5.txt')
)
$md5 = [System.Security.Cryptography.MD5]::Create()
$fouten = 0
foreach ($regel in Get-Content $RefBestand) {
    if ($regel -match '^\s*#' -or $regel -notmatch '^([0-9a-f]{32})\s+(\S+)$') { continue }
    $verwacht = $Matches[1]; $naam = $Matches[2]
    $pad = Join-Path $BronMap $naam
    if (-not (Test-Path $pad)) { Write-Host "ONTBREEKT: $naam"; $fouten++; continue }
    $bytes = [System.IO.File]::ReadAllBytes($pad)
    $tekst = [System.Text.Encoding]::UTF8.GetString($bytes) -replace "`r`n", "`n"
    $hash = ($md5.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($tekst)) |
             ForEach-Object { $_.ToString('x2') }) -join ''
    if ($hash -eq $verwacht) {
        Write-Host ("OK        {0}  {1}" -f $hash, $naam)
    } else {
        Write-Host ("AFWIJKEND {0} (verwacht {1})  {2}" -f $hash, $verwacht, $naam)
        $fouten++
    }
}
if ($fouten -eq 0) {
    Write-Host 'RESULTAAT: alle CoreMark-kernbestanden identiek aan EEMBC upstream 1f483d5.'
    exit 0
} else {
    Write-Host "RESULTAAT: $fouten afwijking(en) - NIET identiek aan upstream."
    exit 1
}
