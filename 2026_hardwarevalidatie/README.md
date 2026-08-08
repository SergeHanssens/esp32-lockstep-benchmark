# Hardwarevalidatie 2026 — opbouw, methodologie en resultaten

Dit deel van de repository documenteert het validatietraject van augustus
2026. Ik bouw de lockstep-kern niet in één keer, maar in kleine, telkens
flashbare stappen: eerst bewijzen dat de omgeving werkt, dan de
inter-core-communicatie karakteriseren (het fundament van elke
lockstep-vergelijking), dan de officiële CoreMark als workload valideren, en
pas daarna de eigenlijke lockstep-kern. Elke stap eindigt met een seriële log
van het echte bord; die logs staan naast de broncode in deze mappen.

## Meetmethodologie

Twee keuzes bepalen alle metingen in dit traject.

**Per-core cycle-counters.** Elke Xtensa-core heeft een eigen CCOUNT-register
en die tellers lopen niet synchroon. Tijdstempels van core 0 en core 1 zijn
dus niet rechtstreeks vergelijkbaar. Daarom meet elke core uitsluitend met
zijn eigen teller en wordt de transporttijd afgeleid:
*transport = rondreis (initiator) − verwerking (responder)*. Door daarna de
rollen om te draaien (core 1 initieert, core 0 antwoordt) meet ik de
symmetrie in plaats van ze aan te nemen; het gemeten verschil tussen beide
richtingen bedraagt 6,9 %.

**Geheugenbarrières.** Bij communicatie via gedeeld geheugen plaatst de
schrijvende core een `memw`-instructie tussen het schrijven van de data en
het zetten van de vlag, zodat de leesvolgorde op de andere core gegarandeerd
is. Dit patroon (data → memw → vlag) is hetzelfde dat de lockstep-kern
gebruikt.

## Projecten en resultaten (alle logs van 8 augustus 2026, op het bord gemeten)

| Project | Vraag | Resultaat (log als bewijs) |
|---|---|---|
| `hello_world/` | werkt de omgeving? | boot van beide cores ok (`boot_log_20260808.txt`) |
| `02_pingpong/` | rondreislatentie via gedeeld geheugen + `memw` | min/gem/max 62/96/402 cycli @160 MHz (`pingpong_log_20260808.txt`) |
| `02_pingpong_rtos/` | zelfde rondreis via FreeRTOS-queues | 5299/5656/6278 cycli, ≈60× trager (`pingpong_rtos_log_20260808.txt`) |
| `03_pingpong_oneway/` | rondreis ontleed in transport en verwerking, beide richtingen | enkele reis ≈ 20,2 cycli ≈ 0,126 µs; symmetrie 6,9 %; alle maxima vallen in ronde 0 (`oneway_rondeindex_log_20260808.txt`) |
| `03_pingpong_oneway_rtos/` | zelfde ontleding via queues | enkele reis ≈ 1920 cycli ≈ 12 µs, ≈95× trager (`oneway_rtos_log_20260808.txt`) |
| `04_coremark/` | officiële EEMBC CoreMark, baseline op core 0 | pass A (RTOS actief) 584,75 it/s; pass B (scheduler geschorst, interrupts uit) 585,06 it/s; crcfinal 0x382f (`coremark_baseline_log_20260808.txt`) |
| `04_coremark_max_RTOS/` | aparte firmware, vol RTOS, watchdogs aan | 581,28 it/s, geldig (`coremark_max_rtos_log_20260808.txt`) |
| `04_coremark_min_RTOS/` | aparte firmware, minimale modus | 585,06 it/s, geldig (`coremark_min_rtos_log_20260808.txt`) |
| `metingen/coremark_runs_20260808/` | reproduceerbaarheid (checkpoint 1) | 10 resets × 2 passes = 20 geldige runs in CSV; spreiding < 0,0002 it/s |

Drie bevindingen verdienen toelichting, omdat ze het verschil tonen tussen
een getal rapporteren en een getal begrijpen.

**Uitschieters zijn koude-start-effecten.** De maxima in de
shared-memory-metingen (rondreis 543 cycli waar het gemiddelde 63,8 is) leken
eerst op timer-interruptverstoring. Instrumentatie met een ronde-index wees
uit dat álle maxima in ronde 0 van de eerste meetfase vallen; zodra caches
warm zijn (fase 2) piekt de rondreis nog op amper 62 cycli. De eerdere
hypothese "timer-interrupt" heb ik daarmee verworpen.

**De −0,6 % bij volle RTOS is verklaard, niet weggewuifd.** Met watchdogs aan
zakt de score van 584,75 naar 581,28 it/s. Oorzaak, onderbouwd met de
IDF-broncode én de eigen log: de task-watchdog is één hardware-timer (MWDT0)
waarvan de interrupt vast op core 0 gealloceerd wordt; elke 5 s print de
wdt-ISR blokkerend zijn volledige rapport op precies de core waar CoreMark
draait (de log toont letterlijk "Print CPU 0 (current core) backtrace").

**De CoreMark is echt en actueel.** De gebruikte bronbestanden zijn per
MD5-hash byte-identiek aan de laatste commit op
[github.com/eembc/coremark](https://github.com/eembc/coremark)
(`1f483d5`, 1 mei 2025). De kruisvalidatie klopt bovendien intern: pass B en
de aparte minimale firmware geven exact dezelfde score (585,06 it/s), zoals
het hoort wanneer beide effectief zonder scheduler-verstoring draaien. Ter
referentie: Espressif rapporteert 613,86 voor de S3 op 240 MHz single-core;
het verschil (≈5 %) schrijf ik voorlopig toe aan `-O2` tegenover hun
buildflags — een `-O3`-run staat op de lijst om dit te duiden.

Let op bij het vergelijken: de 02/03-projecten draaien op 160 MHz
(IDF-default), de 04-projecten op 240 MHz (`sdkconfig.defaults`).
Cycli vergelijken kan; de omrekening naar µs verschilt.

## Meetprotocol checkpoint 1 (reproduceerbaar)

Het script `scripts/meet_coremark_runs.ps1` flasht `04_coremark` en start
`scripts/meet_coremark_runs.py`, dat per run het bord hard reset via RTS, de
seriële uitvoer capteert tot de eindmarker, beide passes parst en wegschrijft
naar een CSV met datum. Per run wordt ook de ruwe log bewaard; een run die
niet parst krijgt de markering ONGELDIG in plaats van een verzonnen waarde.
De sessie van 8 augustus 2026 (10u49–11u01): 10 resets, 20 geldige metingen,
alle met "Correct operation validated" en crcfinal 0x382f.

## Zelf reproduceren

1. ESP-IDF v5.5-omgeving activeren, bord op een COM-poort (hier COM3).
2. In een projectmap: `idf.py build` en `idf.py -p COM3 flash`.
3. Log capteren: `python scripts/capture_generic.py COM3 <seconden> <logbestand>`
   (reset het bord via RTS en schrijft de volledige uitvoer weg).
4. Voor de CoreMark-reeks: `powershell -File scripts/meet_coremark_runs.ps1 -Runs 10`.
