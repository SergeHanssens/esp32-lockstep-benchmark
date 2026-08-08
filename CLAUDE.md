# Werkregels voor AI-sessies in deze repository

Dit bestand stuurt elke AI-werksessie (Claude) in dit project. De regels
zijn niet onderhandelbaar; ze bestaan omdat in 2025 AI-gegenereerde
resultaten onvoldoende gevalideerd werden.

1. **Hardware-first.** Niets is "af" zonder seriële log of meting van het
   echte bord (ESP32-S3-DevKitC-1, COM3). Nooit cijfers verzinnen of
   extrapoleren; een mislukte meting wordt als mislukt gerapporteerd.
2. **Officiële EEMBC CoreMark**, geen stub. Bronnen staan geverifieerd
   (MD5 t.o.v. upstream `1f483d5`).
3. **Scope bewaken:** lockstep-kern → CoreMark-overhead → foutinjectie.
   Geen rollback, heartbeat, Tracealyzer of RISC-V-uitbreidingen. De
   synchronisatie is bewust een minimaal, meetbaar prototype (volatile +
   memw + spin-wait, geen timeouts/recovery); die beperking wordt in de
   thesis expliciet benoemd, niet stilzwijgend genegeerd. "Lockstep" =
   softwarematige checkpoint-lockstep (zie README's), geen cycle-accurate
   lockstep — conclusies binnen dat model houden.
4. **Kleine stappen, alles flashbaar.** Log met datum bij elk resultaat, in
   de projectmap. Eén taak tegelijk.
5. **Bewijs tonen, onzekerheid benoemen.** Verklaringen onderbouwen met
   broncode of meting, nooit "uit het hoofd". Resultaten nooit mooier
   voorstellen dan ze zijn.
6. **Transparantie en auteurschap.** Commits staan uitsluitend op naam van
   Serge: een AI kan geen auteursverantwoordelijkheid dragen en hoort dus
   niet in de commit-metadata. AI-assistentie wordt verantwoord in
   `2026_hardwarevalidatie/AI_VERANTWOORDING.md` en per sessie in het
   logboek. Serge valideert en committeert zelf.

Praktisch: ESP-IDF v5.5 via PowerShell-profiel (`$env:TMP='D:\temp'`);
buildscripts als `.ps1` wegschrijven en met `powershell -File` draaien;
capture via `2026_hardwarevalidatie/scripts/capture_generic.py` (RTS-reset).
