# Logboek hardwarevalidatie — augustus 2026

Dit logboek houdt per werkdag bij wat ik deed, waarom ik het zo aanpakte, en
welk bewijsstuk de dag oplevert. Het is bewust geschreven als
gedachtengang, niet als gepolijst eindverslag: beslissingen, verworpen
hypotheses en bijgestuurde plannen horen er net bij. De regel uit de
hoofd-README geldt overal: geen log van het bord, geen resultaat.

## Donderdag 6 – vrijdag 7 augustus — orde scheppen vóór er gemeten wordt

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
cycli; via FreeRTOS-queues wordt dat ≈60× meer. Dat verschil is geen
detail — het bepaalt de architectuurkeuze van de checker: het meetpad van de
lockstep loopt via gedeeld geheugen, FreeRTOS dient alleen om taken op te
starten.

**Middag: meten is één ding, begrijpen is het echte werk.** De rondreis heb
ik vervolgens ontleed (transport versus verwerking) door elke core enkel met
zijn eigen cycle-counter te laten meten en daarna de richting om te keren:
enkele reis ≈ 20,2 cycli ≈ 0,126 µs, symmetrie tussen beide richtingen
6,9 %. In de eerste metingen zaten uitschieters (rondreis max 543 cycli)
waarvoor ik eerst timer-interrupts verdacht. Die hypothese heb ik niet
opgeschreven maar getest: na instrumentatie met een ronde-index blijkt élk
maximum in ronde 0 van de eerste fase te vallen — koude caches, geen
interrupts. Fase 2, met warme caches, piekt op 62 cycli. Een verworpen
hypothese met bewijs is hier meer waard dan een juiste gok.

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

**Middag: checkpoint 1 binnen, een dag vóór de deadline.** Meetscript
geschreven dat per run het bord hard reset en beide passes naar CSV parst;
eerst een smoke-test met één run, daarna de volledige sessie: 10 resets, 20
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
een bewuste bitflip door de applicatiecore in ronde 500. Een detector die
nog nooit iets gedetecteerd heeft, bewijst niets; deze ene gecontroleerde
fout bewijst de hele detectieketen. Resultaat van de run (1000 rondes,
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

**Bewijs van vandaag:** alle logs met datum 20260808 in de projectmappen
(inclusief `05_lockstep_kern/lockstep_kern_log_20260808.txt`), de CSV in
`metingen/coremark_runs_20260808/`, en de commits van deze dag.

## Volgende stappen (in volgorde, één taak tegelijk)

1. Foutinjectie (bitflips van buitenaf, bv. timer-ISR) met aantoonbare
   detectiegraad en -latentie in de log.
2. Overheadmeting beschermd versus onbeschermd met het bestaande
   CoreMark-meetprotocol, meermaals herhaald, CSV in de repo.
3. Parallel: thesistekst bijwerken naar wat werkelijk gemeten is;
   `-O3`-run om het verschil met de Espressif-referentie te duiden.
