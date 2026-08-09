# Logboek hardwarevalidatie — augustus 2026

Dit logboek houdt per werkdag bij wat ik deed, waarom ik het zo aanpakte, en
welk bewijsstuk de dag oplevert. Het is bewust geschreven als
gedachtengang, niet als gepolijst eindverslag: beslissingen, verworpen
hypotheses en bijgestuurde plannen horen er net bij. De regel uit de
hoofd-README geldt overal: geen log van het bord, geen resultaat.

## Donderdag 6 – vrijdag 7 augustus — orde scheppen voor er gemeten wordt

Voor ik één regel nieuwe code schreef, heb ik al het bestaande
thesismateriaal van drie machines (oude desktop, laptop, externe SSD) en
Google Drive samengebracht, geïnventariseerd en geordend. De belangrijkste
opbrengst was niet de kopie zelf, maar de eerlijke scheiding die ze mogelijk
maakte: wat is verdedigbaar eigen werk (de bare-metal dual-core opstart met
eigen linkerbestand uit december 2024, de gemeten latentievergelijking
FreeRTOS-API versus gedeeld geheugen, de onderzoeksnotities), en wat vervalt
als niet-gevalideerd (de resultaatclaims uit 2025, de CoreMark-stub in deze
repository). Die scheiding bepaalt de scope van dit traject: een werkende
lockstep-kern, een echte CoreMark-overheadmeting en foutinjectie — en bewust
niets daarnaast. Zelfde dagen: werkplek en ESP-IDF v5.5-omgeving op de
desktop opgezet, mail met stand van zaken en dagplan aan de promotor
voorbereid, met twee checkpoints als go/no-go: CoreMark-baseline op zondag
9/8, werkende lockstep op dinsdag 11/8.

## Zaterdag 8 augustus — van "boot ok" tot gevalideerde CoreMark-baseline

**Ochtend: omgeving en synchronisatie-fundament.** Eerst `hello_world`
geflasht als bewijs dat toolchain, bord en seriële capture werken
(`boot_log_20260808.txt`). Daarna de kern van elke lockstep-vergelijking
gekarakteriseerd: hoe snel kunnen twee cores elkaar iets melden? Via gedeeld
geheugen met `memw`-barrière en spin-wait kost een rondreis gemiddeld 96
cycli; via FreeRTOS-queues wordt dat ≈60× meer. Dat verschil
bepaalt de architectuurkeuze van de checker: het meetpad van de
lockstep loopt via gedeeld geheugen, FreeRTOS dient alleen om taken op te
starten.

**Middag: de rondreis ontleed in transport en verwerking.** De rondreis heb
ik vervolgens ontleed (transport versus verwerking) door elke core enkel met
zijn eigen cycle-counter te laten meten en daarna de richting om te keren:
enkele reis ≈ 20,2 cycli ≈ 0,126 µs, symmetrie tussen beide richtingen
6,9 %. In de eerste metingen zaten uitschieters (rondreis max 543 cycli)
waarvoor ik eerst timer-interrupts verdacht. Die hypothese heb ik niet
opgeschreven maar getest: na instrumentatie met een ronde-index blijkt elk
maximum in ronde 0 van de eerste fase te vallen — koude caches, geen
interrupts. Fase 2, met warme caches, piekt op 62 cycli. De timer-hypothese kon ik
zo met bewijs verwerpen.

**Namiddag: officiële CoreMark, kruisgevalideerd.** De EEMBC-bronnen eerst
per MD5 vergeleken met upstream (byte-identiek aan commit `1f483d5` van
1 mei 2025) — na de stub-les van 2025 wil ik dat zwart op wit. Baseline
gemeten in twee passes binnen één firmware (A: RTOS actief, 584,75 it/s;
B: scheduler geschorst en interrupts uit, 585,06 it/s) en gekruist met twee
aparte firmwares: de minimale variant geeft exact pass B (585,06 = 585,06),
de volle RTOS-variant met watchdogs zakt naar 581,28 it/s.
Die −0,6 % heb ik niet laten hangen op een vermoeden: uit de
IDF-broncode (`esp_intr_alloc`, `task_wdt.c`) en de eigen log blijkt dat de
task-watchdog-ISR vast op core 0 gealloceerd is en daar elke 5 s blokkerend
zijn rapport print — op de CoreMark-core dus. Mijn eerdere, lossere
verklaring ("de meldingen printen op dezelfde core") was onvolledig; de
correcte formulering staat nu in de README van dit deel.

**Middag: checkpoint 1 binnen, een dag voor de deadline.** Meetscript
geschreven dat per run het bord hard reset en beide passes naar CSV parst;
eerst één controlerun, daarna de volledige sessie: 10 resets, 20
geldige metingen, spreiding kleiner dan 0,0002 it/s, alle CRC's correct.
Reproduceerbaarheid was het doel van dit checkpoint en die is er:
wie het script draait, krijgt dezelfde CSV-structuur met eigen metingen.

**Namiddag (vervolg): de lockstep-kern zelf — checkpoint 2 drie dagen vroeg.**
Met alle bouwstenen gevalideerd was de kern zelf een kwestie van samenstellen:
de applicatiecore (core 0) genereert per ronde een invoerblok, deelt het met
CRC via gedeeld geheugen (data → memw → vlag), rekent op zijn eigen kopie en
schrijft het resultaat weg; de checkercore (core 1) rekent onafhankelijk mee
en controleert op drie punten — invoer (CRC), verwerking (resultaat-
vergelijking) en uitvoer (terugleescontrole). Eén ontwerpkeuze wil ik hier
expliciet verantwoorden: de firmware bevat een **zelftest van de detector**,
een bewuste bitflip door de applicatiecore in ronde 500. Zo is aantoonbaar dat de volledige
detectieketen effectief werkt, en niet alleen dat ze geen valse alarmen
geeft. Resultaat van de run (1000 rondes,
240 MHz, `lockstep_kern_log_20260808.txt`): exact één gedetecteerde ronde —
ronde 500, verdict VERWERKING+UITVOER zoals voorspeld (de corrupte waarde
propageert naar de uitvoer, de invoer-CRC blijft correct) — en nul valse
positieven in de 999 andere rondes. De kosten: 1229,8 cycli per beschermde
ronde tegenover 263,1 onbeschermd, dus ≈967 cycli vaste overhead per blok.
Bij deze bewust kleine workload is dat relatief veel (+367 %); het is een
vast bedrag per controlecyclus, dus de relatieve overhead zakt naarmate de
workload groeit — precies wat de CoreMark-overheadmeting straks moet
kwantificeren. Ook hier weer: max in ronde 0, het intussen vertrouwde
koude-start-effect.

**Namiddag (slot): foutinjectie op de kern.** Op de kern van
05 heb ik twee injectiecampagnes gebouwd, omdat ze elk een andere vraag
beantwoorden. Campagne A injecteert 333 gerichte enkelvoudige bitflips
(random woord en bit, afwisselend in het gedeelde invoerblok, het
rekenresultaat en de uitvoer) op exact gekende punten in de pijplijn, zodat
detectiegraad en -latentie per foutklasse meetbaar zijn. Resultaat: 100 %
detectie, en — belangrijker — elk doelwit gaf exact het vooraf voorspelde
verdict: een invoerfout propageert door alles (0x7), een resultaatfout
raakt verwerking en uitvoer (0x6), een uitvoerfout alleen de uitvoer (0x4).
De latenties zijn logisch geordend: een invoerfout kost gemiddeld 2,7 µs om
te detecteren (de checker moet eerst zelf rekenen), een resultaat- of
uitvoerfout 1,6 en 1,5 µs. Campagne B laat een esp_timer elke 250 µs een
willekeurige bit flippen terwijl de lockstep 50.000 rondes draait — het
realistische SEU-scenario zonder gecontroleerd injectiemoment. Daar is de
detectiegraad 18,5 % (210 van 1133): een flip wordt alleen gezien als hij
in het korte leefvenster van de data valt; daarbuiten wordt hij door de
volgende ronde overschreven — maskering, precies zoals bij echte
single-event upsets. Dat die 18,5 % consistent is met de verhouding
venster/rondeduur, en dat alle gedetecteerde rondes verdict 0x7 droegen —
invoerfouten die naar verwerking en uitvoer propageren, zodat de drie
categorietellers elk tot 210 oplopen; logisch, want het invoerblok is 32
van de 34 aangevallen woorden en leeft het langst — verklaart het cijfer. Alle 333
A-injecties staan als CSV in de repo (`foutinjectie_campagneA_20260808.csv`,
geparst uit de log met `scripts/parse_foutinjectie.py`).

**Avond: externe review gevraagd en verwerkt.** Ik heb de repo bewust laten
doorlichten door een onafhankelijke AI-reviewer (OpenAI Codex), als externe
toets op mijn eigen blinde vlekken. De review bevestigde de methodologische
kern (harde scheiding 2025/2026, logs die de cijfers dragen, transparant
AI-gebruik) en legde terecht een aantal gebreken bloot. Wat ik meteen heb
rechtgezet: de 2.890 in 2025 ingecheckte `build/`-artefacten zijn uit `main`
verwijderd (een verse Windows-clone faalde op te lange paden — de
geschiedenis herschrijf ik bewust niet, zodat bestaande commit-links naar
mijn promotor geldig blijven); de MD5-claim over de CoreMark-bronnen is nu
reproduceerbaar via `scripts/verify_coremark_sources.ps1` met een
hashbestand (LF-genormaliseerd, dus autocrlf-bestendig; uitvoer: 6/6 OK);
de campagne-B-verwoording is gecorrigeerd (alle 210 detecties droegen
verdict 0x7 — invoerfouten die propageren — niet "alleen invoer" in enge
zin); de term "lockstep" is in beide README's genuanceerd tot softwarematige
checkpoint-lockstep, met expliciete afbakening van de prototype-
synchronisatie (geen atomics/timeouts/heartbeat — bewust buiten scope, in
de thesis te benoemen als beperking); en het meetsessie-script is
geparametriseerd (poort/paden) met een scripts-README die eerlijk zegt wat
labspoor is. Terechte punten die bewust op de takenlijst blijven in plaats
van nu: CoreMark-in-lockstep (de volgende meting), meerdere seeds/langere
campagnes met betrouwbaarheidsintervallen, en metingen op meerdere borden.

**Bewijs van vandaag:** alle logs met datum 20260808 in de projectmappen
(inclusief `05_lockstep_kern/…` en `06_foutinjectie/…`), de CSV's in
`metingen/` en `06_foutinjectie/`, en de commits van deze dag.

**Avond (slot): reviewclaims tegen primaire bronnen geverifieerd.** De
technische beweringen uit de AI-reviews heb ik niet op gezag aangenomen
maar nagetrokken op deze machine: `esp_cpu_wait_for_intr()` blijkt
letterlijk `asm volatile("waiti 0")` (`xt_utils.h:82`) — een
interrupt-wachtinstructie, geen geheugenbarrière, dus die suggestie uit
review A was fout; interne SRAM is op de S3 niet gecachet
(`SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE` bestaat op de P4, niet op de S3);
en de Xtensa ISA-referentie definieert `MEMW` als "Memory Wait" met
expliciet "intended for implementing C's volatile attribute" (p. 117) —
ons synchronisatiepatroon is dus precies het bedoelde gebruik. Deze
onderbouwing staat nu in de sectie "Beperkingen en geldigheid" van de
README, samen met common-cause-failures en de SPOF-afbakening.

**Avond (governance): auteurschap expliciet vastgelegd.** De commits in
deze repository staan op mijn naam, en dat is een principiële keuze: een
auteur moet verantwoordelijkheid kunnen dragen voor het werk, en een
AI-model kan dat niet. De
transparantie over AI-gebruik staat waar ze hoort: in
`AI_VERANTWOORDING.md`, in dit logboek en straks in de
transparantieverklaring bij de thesis. Ik valideer en committeer alles
zelf; die afspraak is nu ook als werkregel verankerd in CLAUDE.md.

## Zaterdag 8 augustus, avond — het kerncijfer: CoreMark in de lockstep

**Ontwerpbeslissing eerst.** Voor de CoreMark-in-lockstep-meting lagen twee
opties op tafel: duale uitvoering (beide cores een volledige eigen CoreMark,
één vergelijking op het einde) of gesegmenteerde uitvoering (CoreMark in
korte segmenten met een checkpoint na elk segment). Gekozen: beide, in één
firmware — de gesegmenteerde variant als hoofdmeting, de duale als
referentiepunt om de duale-uitvoeringskost te isoleren. Het vijffasenontwerp
(`07_coremark_lockstep`) voegt daar een onbeschermde volle run (in-situ
kruischeck met de 04-baseline), een onbeschermd gesegmenteerde run
(isoleert de segmentatiekost) en een zelftestfase aan toe.

**Kernbestanden ongewijzigd; afwijkingen benoemd.** De zes kernbestanden zijn opnieuw
byte-identiek aan upstream `1f483d5` (verificatiescript uitgebreid naar 07,
12/12 OK). Parallellisme via het officiële `MULTITHREAD`-mechanisme in de
porting-laag: context 0 = applicatiecore, context 1 = checkercore, met het
02/03/05-handshakepatroon (data → `memw` → vlag) voor de
checkpointvergelijking van de vier CRC's. Iteraties per segment via de
officiële runtime-parameter (seed 4). Geheugenmethode STACK i.p.v. STATIC
omdat core_main.c STATIC bij meerdere contexts weigert; dat fase A de
04-baseline exact reproduceert (584,75 it/s) toont dat die keuze de score
niet beïnvloedt.

**Het kerncijfer, met ontleding (10 resets, 18u55–19u22).**
Onbeschermd 584,75 it/s (stddev 0,0000); beschermd gesegmenteerd met 400
checkpoints per run 462,51 it/s (stddev 0,033) → **20,90 % lagere
doorvoer** (21,26 % effectief incl. herinitialisatie). Terminologie
bewust: het gesegmenteerde aggregaat is geen officiële CoreMark-score
(elk segment blijft onder de vereiste 10 s); alleen de volle runs zijn
geldige scores. De vergelijking beschermd/onbeschermd gebeurt op identiek
gesegmenteerde metingen en blijft daarmee zuiver. De ontleding maakt het cijfer
begrijpelijk: duale uitvoering alleen kost al 20,43 % (waarschijnlijke
verklaring: contentie op de gedeelde SRAM-interconnect; niet afzonderlijk
geïsoleerd), segmentatie kost getimed vrijwel niets (0,03 %), en het
checkpointmechanisme zelf ≈ 0,5 procentpunt. De detectie-infrastructuur is
binnen deze configuratie dus goedkoop; de prijs zit in de redundante
uitvoering zelf. 4000
checkpointlatenties binnen de beschermde runs: gemiddeld 2832 cycli
(11,8 µs), P99 6623, P99,9 6908, max 6919 cycli (28,8 µs) — percentielen op
de binnen-run-datapunten, gemiddelde en spreiding over de runscores, zoals
in de statistiekmethodiek vastgelegd. 0 valse positieven over 4100
checkpoints; de zelftest (bewuste bitflip op de gepubliceerde crcfinal in
segment 5) is in alle 10 runs exact gedetecteerd: precies één mismatch, in
precies dat segment, met precies het verwachte verdict.

**Werkwijze als voorheen.** Alles op het bord gemeten; ruwe logs, CSV's en
analyse-uitvoer in `metingen/lockstep_runs_20260808/`; meet- en
analysescripts in `scripts/`. Eerste verificatierun apart bewaard als
`07_coremark_lockstep/coremark_lockstep_log_20260808.txt`.

## Volgende stappen — planning vervroegd (beslist 8/8 avond)

De onderzoekskern staat; de resterende dagen zijn voor de tekst. De
centrale onderzoeksvraag — de overhead van echte CoreMark binnen de
beschermde architectuur — was bewust het eerstvolgende werk en is
zaterdagavond gemeten en ontleed (zie hierboven): het kerncijfer staat.
Nieuwe mijlpalen: **zondag 9/8** volledig voor de thesistekst — alles
uitschrijven wat al geschreven kan worden, met de resultaten van 02 t.e.m.
07 als ruggengraat; **maandag 10/8 ochtend: eerste DRAFT (werkversie) naar de
promotor** — bewust vroeg en met open punten gemarkeerd, zodat hij iets
concreets heeft om zijn beslissing op te baseren; **dinsdag 11/8 avond:
onderzoek volledig afgerond** (onder voorbehoud van bijsturingen), incl.
eventueel de S3-bordensweep als de tijd het toelaat; **woensdag 12/8:
tweede DRAFT ter nalezing**; donderdag t.e.m. zondag uitsluitend
bijsturingen; **maandag 17/8 voor 23u59 indienen**. Optioneel en alleen
als alles op schema zit: langere injectiecampagnes met
betrouwbaarheidsintervallen, `-O3`-duidingsrun.

## Zondag 9 augustus, laat op de avond — validation-seed-run 0x3415 (EEMBC-rapporteerbaarheid)

De EEMBC-runregels vragen naast de performance-run (seeds 0x0/0x0/0x66)
ook een validation-run met seeds 0x3415/0x3415/0x66. Die stond gepland
voor dinsdag 11/8, maar ik heb ze vanavond al uitgevoerd. De wijziging
bleef beperkt tot de porting-laag: een build-optie in `main/CMakeLists.txt`
(`idf.py -D LS_VALIDATION_RUN=1`) die de bestaande VALIDATION_RUN-tak in
`core_portme.c` activeert; de CoreMark-kernbestanden zijn byte-identiek
gebleven. Herbuild, flash en capture liepen via
`scripts/build_flash_capture_07_validation.ps1` (kopie van het 07-script
met de extra vlag en een aparte lognaam).

Resultaat (log `07_coremark_lockstep/coremark_lockstep_validation0x3415_log_20260809.txt`,
build 23u43): de firmware meldt nu "2K validation run parameters for
coremark." waar de run van 8/8 "performance" meldde. Fase A onbeschermd
vol: 20.000 iteraties in 34,25 s, 583,86 it/s, seedcrc 0x18f2, "Correct
operation validated". Fase B beschermd duaal vol: officiële
multithread-uitvoer 925,33 it/s over 43,23 s; beide contexten publiceren
identieke CRC's en valideren correct; 0 mismatches. Beide volle fasen
duren ruim boven de vereiste 10 s. De afwijking t.o.v. de performance-run
van 8/8 is klein (fase A −0,15 %, fase B −0,55 % op de afgeleide
per-contextwaarde) — run-tot-runvariatie plus het andere datapatroon van
de validation-seeds. De "CoreMark 1.0"-scoreregel verschijnt in deze log
bewust niet: CoreMark drukt die alleen af bij performance-seeds. De score
blijft dus uit de performance-run van 8/8 komen; de validation-run maakt
het paar rapporteerbaar volgens de run and reporting rules. De
segmentfasen en de zelftest liepen mee en gedroegen zich zoals op 8/8
(0 valse mismatches; de bewuste bitflip in segment 5 exact gedetecteerd:
precies één mismatch, in precies dat segment).

Hiermee is de open meettaak van dinsdag 11/8 afgerond; de S3-bordensweep
blijft expliciet optioneel.
