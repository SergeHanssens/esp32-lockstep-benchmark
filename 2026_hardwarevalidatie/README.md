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

**Terminologie en afbakening.** "Lockstep" is hier steeds softwarematige
checkpoint-lockstep: onafhankelijk rekenende cores, vergeleken op
gedefinieerde controlepunten — geen cycle-accurate of instructie-synchrone
lockstep. De synchronisatie (volatile + `memw` + spin-wait) is een bewust
minimale, meetbare prototype-synchronisatie: geen C11-atomics, geen
timeouts, geen heartbeat of recovery bij een uitgevallen checker. Die
mechanismen staan expliciet buiten scope (zie CLAUDE.md); voor een
productierijpe veiligheidsarchitectuur zouden ze nodig zijn, en de thesis
benoemt dat als beperking en toekomstwerk.

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
| `05_lockstep_kern/` | de lockstep-kern zelf (checkpoint 2): app-core rekent, checker-core controleert invoer/verwerking/uitvoer | 1000 rondes @240 MHz: 0 valse positieven; detector-zelftest (bewuste bitflip in ronde 500) exact gedetecteerd met verdict VERWERKING+UITVOER; 1229,8 vs 263,1 cycli/ronde → ≈967 cycli vaste overhead per controlecyclus (`lockstep_kern_log_20260808.txt`) |
| `06_foutinjectie/` | foutinjectie op de kern: campagne A = 333 gerichte bitflips (invoer/resultaat/uitvoer, random woord+bit, latentiemeting); campagne B = 1133 asynchrone flips via esp_timer over 50.000 rondes | A: **100 % detectie**, elk doelwit exact het voorspelde verdict (invoer→0x7, resultaat→0x6, uitvoer→0x4); latentie gem 2,7 / 1,6 / 1,5 µs. B: 210 gedetecteerde rondes (18,5 %), alle met verdict 0x7 — invoerfouten die naar verwerking en uitvoer propageren, waardoor de drie categorietellers elk tot 210 oplopen; 81,5 % gemaskeerd, consistent met het korte leefvenster van de data per ronde (`foutinjectie_log_20260808.txt`, CSV `foutinjectie_campagneA_20260808.csv`) |
| `07_coremark_lockstep/` | het kerncijfer: overhead van echte CoreMark binnen de checkpoint-lockstep | vijf fasen per run (onbeschermd vol / beschermd duaal vol / onbeschermd gesegmenteerd / beschermd gesegmenteerd 400×50 iteraties / zelftest); onbeschermd 584,75 it/s = exact de 04-baseline; beschermd gesegmenteerd 462,51 it/s → **20,90 % lagere doorvoer** (gesegmenteerd aggregaat, geen officiële CoreMark-score — zie toelichting onder de tabel; getimed; 21,26 % effectief incl. herinitialisatie); ontleding: duale uitvoering 20,43 %, segmentatie 0,03 %, checkpointmechanisme ≈ 0,5 procentpunt; beide volle runs "Correct operation validated", crcfinal 0x382f op beide cores; zelftest-bitflip 10/10 exact gedetecteerd (`coremark_lockstep_log_20260808.txt`) |
| `metingen/lockstep_runs_20260808/` | reproduceerbaarheid kerncijfer | 10 resets × 5 fasen in CSV; stddev over runscores ≤ 0,10 it/s; 4000 checkpointlatenties binnen de beschermde runs: gem 2832 cycli (11,8 µs), P50 3826, P99 6623 (27,6 µs), P99,9 6908 (28,8 µs), max 6919; 0 valse positieven over 4100 checkpoints (`lockstep_fasen_20260808.csv`, `lockstep_checkpoints_20260808.csv`, `analyse_lockstep_20260808.txt`) |

Vier bevindingen verdienen extra toelichting.

**Uitschieters zijn koude-start-effecten.** De maxima in de
shared-memory-metingen (rondreis 543 cycli waar het gemiddelde 63,8 is) leken
eerst op timer-interruptverstoring. Instrumentatie met een ronde-index wees
uit dat alle maxima in ronde 0 van de eerste meetfase vallen; zodra caches
warm zijn (fase 2) piekt de rondreis nog op amper 62 cycli. De eerdere
hypothese "timer-interrupt" heb ik daarmee verworpen.

**De −0,6 % bij volle RTOS is verklaard, niet weggewuifd.** Met watchdogs aan
zakt de score van 584,75 naar 581,28 it/s. Oorzaak, onderbouwd met de
IDF-broncode en de eigen log: de task-watchdog is één hardware-timer (MWDT0)
waarvan de interrupt vast op core 0 gealloceerd wordt; elke 5 s print de
wdt-ISR blokkerend zijn volledige rapport op precies de core waar CoreMark
draait (de log toont letterlijk "Print CPU 0 (current core) backtrace").

**De CoreMark is echt en actueel.** De zes algoritmische kernbestanden
(`coremark.h`, `core_main.c`, `core_list_join.c`, `core_matrix.c`,
`core_state.c`, `core_util.c`) zijn per MD5-hash byte-identiek aan de
laatste commit op
[github.com/eembc/coremark](https://github.com/eembc/coremark)
(`1f483d5`, 1 mei 2025); de porting-laag (`core_portme.c/.h`) is, zoals de
EEMBC-runregels voorzien, eigen werk voor dit platform. De kruisvalidatie klopt bovendien intern: pass B en
de aparte minimale firmware geven exact dezelfde score (585,06 it/s), zoals
het hoort wanneer beide effectief zonder scheduler-verstoring draaien. Ter
referentie: Espressif rapporteert 613,86 voor de S3 op 240 MHz single-core;
het verschil (≈5 %) schrijf ik voorlopig toe aan `-O2` tegenover hun
buildflags — een `-O3`-run staat op de lijst om dit te duiden.

**De lockstep-overhead is vrijwel volledig duale-uitvoeringskost.** Het
vierfasenontwerp van `07_coremark_lockstep` ontleedt het kerncijfer
(20,90 % lagere doorvoer t.o.v. de baseline-score) in zijn componenten: alleen al
beide cores simultaan dezelfde CoreMark laten draaien kost 20,43 %
(waarschijnlijke verklaring: contentie op de gedeelde geheugen- en
interconnectresources — beide cores vragen continu toegang tot dezelfde
matrix van SRAM-banken; deze microarchitecturale oorzaak is niet
afzonderlijk experimenteel geïsoleerd); de opdeling in 400 segmenten kost
in getimede termen vrijwel niets (0,03 %); en het eigenlijke
checkpointmechanisme (CRC's publiceren, vergelijken, verdict terug) voegt
daar nog ≈ 0,5 procentpunt aan toe. De detectie-infrastructuur zelf is
binnen deze configuratie dus goedkoop; de prijs zit in de redundante
uitvoering — inherent aan elke
duale-uitvoeringsarchitectuur, ook hardware-lockstep. De
checkpointlatentie (publicatie app-core tot verdict ontvangen) omvat naast
handshake en vergelijking ook de restsynchronisatie tussen beide cores en
blijft over 4000 checkpoints onder de 29 µs (P99,9 6908 cycli).
Parallellisme loopt via het officiële EEMBC-`MULTITHREAD`-mechanisme
(`core_start_parallel`/`core_stop_parallel` in de porting-laag); het
aantal iteraties per segment is de officiële runtime-parameter (seed 4).
Twee bewuste afwijkingen t.o.v. de 04-port, beide gerapporteerd in de
output. Eén binnen de EEMBC-regels: geheugenmethode STACK i.p.v. STATIC
(core_main.c staat STATIC niet toe bij meerdere contexts); dat fase A de
04-baseline exact reproduceert, toont dat die keuze de score niet
beïnvloedt. Eén afwijking van de rapporteerbaarheidsregels, en dat benoem
ik expliciet: elk segment draait ver onder de door EEMBC vereiste
10 seconden, dus het gesegmenteerde aggregaat (462,51 it/s) is geen
officiële, rapporteerbare CoreMark-score maar een van CoreMark afgeleide
workload-doorvoer. De poort telt de verwachte 10-secondenmelding per kort
segment daarom niet als fout (elke andere ERROR-regel wel). De vergelijking
blijft zuiver omdat beschermd en onbeschermd gesegmenteerd identiek gemeten
worden; alleen fase A (volle run, ruim 10 s) is een geldige CoreMark-score.

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

## Meetprotocol kerncijfer (CoreMark-in-lockstep)

Zelfde patroon voor `07_coremark_lockstep`:
`scripts/build_flash_capture_07.ps1` bouwt, flasht en capteert één
verificatierun; `scripts/meet_lockstep_runs.py COM3 10` draait daarna de
reeks (per run een harde reset, vijf fasen, ± 165 s) en schrijft twee
CSV's: één rij per fase per run en één rij per checkpoint (segmentduur,
latentie in cycli, verdict). `scripts/analyse_lockstep.py` berekent daaruit
gemiddelde en standaarddeviatie over de runscores, de overheadontleding en
de percentielen (P50/P99/P99,9) over de gepoolde checkpointlatenties —
conform de statistiekmethodiek in de beperkingen-sectie. De firmware print
de samenvattingen ook zelf (regels `FASE07;`/`CSV07;`); de parser neemt
uitsluitend die regels over en bewaart per run de ruwe log. De sessie van
8 augustus 2026 (18u55–19u22): 10 resets, 50 geldige faserijen, 8100
checkpointrijen, 0 valse positieven, zelftest 10/10.

## Zelf reproduceren

1. ESP-IDF v5.5-omgeving activeren, bord op een COM-poort (hier COM3).
2. In een projectmap: `idf.py build` en `idf.py -p COM3 flash`.
3. Log capteren: `python scripts/capture_generic.py COM3 <seconden> <logbestand>`
   (reset het bord via RTS en schrijft de volledige uitvoer weg).
4. Voor de CoreMark-reeks: `powershell -File scripts/meet_coremark_runs.ps1 -Runs 10`.
5. Voor het kerncijfer: `powershell -File scripts/build_flash_capture_07.ps1`,
   daarna `python scripts/meet_lockstep_runs.py COM3 10` en
   `python scripts/analyse_lockstep.py <fasen-csv> <checkpoints-csv>`.

## Beperkingen en geldigheid (limitations & threats to validity)

Deze sectie benoemt expliciet wat dit werk niet aantoont — mede naar
aanleiding van twee externe AI-reviews (Codex, Gemini) waarvan de
technische claims stuk voor stuk tegen primaire bronnen zijn geverifieerd.

**Common cause failures.** Beide cores delen dezelfde silicium-die,
dezelfde voeding en dezelfde klokbron. Een spanningsdip, klok-glitch of
temperatuureffect raakt ze samen en kan identieke fouten veroorzaken die
een vergelijkende checker per definitie niet ziet. Echte
veiligheidsarchitecturen (bv. hardware-lockstep in automotive-MCU's)
gebruiken daarom fysieke scheiding, tijdsverschuiving tussen de cores of
diversiteit. Dit prototype claimt daar niets over.

**Single point of failure, geen timeouts.** De handshake heeft bewust geen
timeout of heartbeat: hangt de checker, dan spint de applicatiecore
oneindig op `vlag_klaar`. Voor het meetdoel (overhead en detectiegedrag
kwantificeren) is dat een aanvaardbare vereenvoudiging; voor een
productiearchitectuur is een onafhankelijke bewaker (externe watchdog of
arbiter) noodzakelijk. Bewuste scopekeuze, zie CLAUDE.md.

**Waarom volatile + `memw` hier in de praktijk werkt — en waarom het
formeel geen C11-correcte synchronisatie is (bron-onderbouwd).** Op de
ESP32-S3 gaat CPU-toegang tot interne SRAM niet door een cache: de
D/I-cache bedient flash en PSRAM (ESP-IDF-docs `memory-types.rst`,
`external-ram.rst`), en de soc-capability
`SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE` bestaat wel voor de ESP32-P4 maar
niet voor de S3 (`components/soc/*/soc_caps.h`). Het klassieke
cache-coherentieprobleem speelt hier dus niet. `MEMW` ("Memory Wait",
Xtensa ISA Reference Manual, p. 490) dwingt af dat alle voorafgaande
geheugentoegangen afgerond zijn voor de volgende beginnen (p. 61); de ISA
vermeldt expliciet dat MEMW "intended for implementing C's volatile
attribute" is en niet voor high-performance multiprocessorsynchronisatie
(p. 117) — precies het gebruik in dit prototype. Op taalniveau geldt
daarbij: alle gedeelde velden in het kanaal zijn `volatile`, en de
C-standaard verbiedt de compiler om volatile-toegangen onderling te
herordenen; die garantie dekt niet eventuele niet-volatile toegangen
eromheen. Twee formele kanttekeningen horen daarbij. Ten eerste is
gelijktijdige niet-atomaire toegang tot gedeelde objecten naar de letter
van C11 een data race; dat dit patroon hier correct werkt, steunt op
implementatie-eigenschappen (uitgelijnde 32-bit-toegangen zijn op de LX7
ondeelbaar, alle gedeelde velden zijn volatile, geen cache op intern
SRAM), niet op een taalgarantie. Ten tweede heeft
`asm volatile("memw")` in deze code geen `"memory"`-clobber en is het dus
geen algemene compilerbarrière; de ordening steunt op de
volatile-kwalificatie van alle gedeelde velden. Dit is bewust
prototypecode: empirisch gevalideerd op deze toolchain en dit silicium.
De formeel correcte weg (L32AI/S32RI, of C11-atomics met
acquire/release-semantiek en `atomic_thread_fence`) is de aangewezen route
voor een productie-implementatie en staat als toekomstig werk genoteerd.

**Meetcontext.** Alle metingen komen van één bord, één dag, één
toolchain/configuratie: sterk als proof-of-concept met bewezen
reproduceerbaarheid binnen die opstelling (10 resets, spreiding
< 0,0002 it/s), zwak voor generalisatie over exemplaren, temperatuur of
buildflags. Campagne A bewijst de detectieketen (injecties op detecteerbare
momenten); campagne B kwantificeert maskering bij asynchrone
software-injectie — geen van beide simuleert fysieke SEU's, EM-glitches of
spanningsdips. De software-injector van campagne B is bovendien intrusief:
de esp_timer-callback draait via de standaard task-dispatch in de
hoogprioritaire esp_timer-taak op core 0 (er is geen `dispatch_method`
gezet, dus geen ISR-dispatch), en deelt daarmee CPU en scheduler met het
gemeten systeem. Het injectiemoment hangt zo mede af van
FreeRTOS-preëmptie en taakvertraging; de gemeten 18,5 % is daarom een
eigenschap van dit software-injectie-experiment, geen zuivere schatting
van detectie bij willekeurig getimede fysieke bitflips — inherent aan
softwarematige foutinjectie zonder externe hardware-glitcher.

**Statistiek.** Gerapporteerd worden min/gemiddelde/max per meetreeks. Een
gemeten maximum is een *high-water mark*, geen formele WCET (die vereist
statische analyse). Bij de CoreMark-overheadmeting geldt: percentielen
(P99/P99,9) worden berekend op de per-checkpoint-latenties binnen de runs
(duizenden datapunten); over de ≥10 runscores zelf — te weinig punten voor
zinvolle percentielen — worden gemiddelde en standaarddeviatie/spreiding
gerapporteerd.
