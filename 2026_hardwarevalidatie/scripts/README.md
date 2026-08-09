# Scripts — labspoor, met parameters waar het telt

Deze scripts zijn het meetspoor van het validatietraject: ze documenteren
exact hoe elke meting tot stand kwam. Het zijn labscripts, geschreven voor
de meetopstelling van de auteur (Windows, ESP-IDF v5.5 via
PowerShell-profiel, bord op een COM-poort). De defaults verwijzen naar die
opstelling; wie repliceert past de parameters of de padvariabelen bovenaan
aan.

| Script | Doel | Parametriseerbaar |
|---|---|---|
| `capture_generic.py` | seriële capture met RTS-reset | poort, duur, logbestand (argumenten) |
| `meet_coremark_runs.py` | n CoreMark-runs → CSV (checkpoint 1) | poort, aantal runs, max duur (argumenten) |
| `meet_coremark_runs.ps1` | flash + volledige meetsessie | `-Runs`, `-Poort`, `-ProjectMap`, `-IdfProfiel` |
| `parse_foutinjectie.py` | INJ-regels uit 06-log → CSV | log- en CSV-pad (argumenten) |
| `meet_lockstep_runs.py` | n 07-runs (5 fasen) → fasen-CSV + checkpoints-CSV | poort, aantal runs, max duur (argumenten) |
| `analyse_lockstep.py` | analyse 07-CSV's: gem/stddev over runs, overheadontleding, P50/P99/P99,9 over checkpointlatenties | fasen-CSV, checkpoints-CSV (argumenten) |
| `verify_coremark_sources.ps1` | MD5-check CoreMark-bronnen (04 én 07) t.o.v. EEMBC upstream `1f483d5` | `-BronMappen`, `-RefBestand`; hashes in `coremark_md5_upstream_1f483d5.txt` |
| `build_flash_capture_05.ps1` / `_06.ps1` / `_07.ps1` / `flash_capture_new3.ps1` / `build_flash_capture_03oneway.ps1` | build + flash + capture per project | paden bovenaan het script (labspoor, bewust eenvoudig) |

De hashes in `coremark_md5_upstream_1f483d5.txt` zijn berekend over
LF-genormaliseerde inhoud, zodat de verificatie ook slaagt op een Windows-
checkout met git-autocrlf.
