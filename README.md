# ESP32-S3 software-lockstep — masterproef ZA0324

Deze repository hoort bij mijn masterproef **"Software lockstep op de ESP32-S3"**
(Master industriële wetenschappen: elektronica-ICT, KU Leuven — Faculteit
Industriële Ingenieurswetenschappen, campus Geel; promotor prof. dr. ing.
Jeffrey Prinzie). Het doel is een softwarematige lockstep waarbij één core de
eigenlijke berekeningen uitvoert (applicatiecore) en de tweede core controleert
op fouten bij het inlezen, verwerken en wegschrijven van data (checkercore).
De EEMBC CoreMark-benchmark dient als workload om de overhead van die
foutdetectie te kwantificeren.

## Twee fasen, één bewijsregel

Deze repository bevat twee duidelijk gescheiden fasen, en ik kies er bewust
voor om ze allebei te laten staan.

**Fase 2025** (`01_esp_lockstep_minimal/`, `02_enhanced_lockstep/`,
`03_enhanced_lockstep_final/`, `docs/`) is het exploratie- en ontwerpwerk van
september 2025: een minimale dual-core opzet, een uitgebreide versie met
timing-analyse en CSV-logging, en een modulaire eindversie. Dit werk heeft
me de architectuur en de synchronisatiepatronen opgeleverd waarop ik nu
verder bouw, maar het heeft één fundamenteel gebrek dat ik hier expliciet
benoem: de "CoreMark" in die mappen is een **stub** (geen echte EEMBC-code) en
de toenmalige resultaten zijn nooit op echte hardware gevalideerd. Die
vaststelling heb ik zelf gemaakt en gerapporteerd aan mijn promotor. De
2025-mappen blijven daarom staan als procesdocumentatie — als ontwerp, niet
als resultaat.

**Fase 2026** (`2026_hardwarevalidatie/`) is de hervalidatie die daaruit
volgt, met één regel die alles stuurt:

> **Elk getal komt uit een meting op het echte bord, met de ruwe log als
> bewijs in deze repository. Geen log, geen resultaat.**

Alle metingen draaien op een ESP32-S3-DevKitC-1 (N16R8) met ESP-IDF v5.5 en
de officiële, tegen upstream geverifieerde EEMBC CoreMark. De opbouw, de
tussenresultaten en de motivering van elke stap staan in
[`2026_hardwarevalidatie/LOGBOEK.md`](2026_hardwarevalidatie/LOGBOEK.md);
de methodologie en resultaten in
[`2026_hardwarevalidatie/README.md`](2026_hardwarevalidatie/README.md).

## Structuur

| Map | Fase | Inhoud |
|---|---|---|
| `01_esp_lockstep_minimal/` | 2025 | minimale dual-core opzet met eenvoudige synchronisatie |
| `02_enhanced_lockstep/` | 2025 | timing-analyse, CSV-logging en checker (workload = stub) |
| `03_enhanced_lockstep_final/` | 2025 | modulaire eindversie 2025 (workload = stub) |
| `docs/` | 2025 | bordfoto's, schema's en de [originele README](docs/README_2025_origineel.md) |
| `2026_hardwarevalidatie/` | 2026 | stapsgewijze validatie op hardware: sync-primitieven, latentie-ontleding, echte CoreMark, meetscripts, logs en CSV |

## Transparantie over AI-gebruik

Bij dit project zet ik generatieve AI transparant in als hulpmiddel, conform
de geldende KU Leuven Code of Conduct GenAI. Wat AI deed, wat ik zelf deed
en hoe elke AI-bijdrage op de hardware gevalideerd wordt, staat beschreven
in
[`2026_hardwarevalidatie/AI_VERANTWOORDING.md`](2026_hardwarevalidatie/AI_VERANTWOORDING.md)
en per werksessie in het
[logboek](2026_hardwarevalidatie/LOGBOEK.md). De commits staan op mijn
naam: ik valideer en committeer elke wijziging zelf, omdat alleen een
menselijke auteur verantwoordelijkheid kan dragen voor het werk.

## Hardware en tooling

- ESP32-S3-DevKitC-1 (N16R8), aangesloten via USB-Serial-JTAG
- ESP-IDF v5.5 (Windows, PowerShell-omgeving), GCC 14.2.0, `-O2`
- Python 3.11 + pyserial voor seriële capture en meetscripts
- EEMBC CoreMark, geverifieerd tegen [github.com/eembc/coremark](https://github.com/eembc/coremark)

## Licentie

MIT — zie [LICENSE](LICENSE).
