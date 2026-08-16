# Canoniek datasetmanifest — negenbordencampagne en dragende datasets

Dit manifest legt vast welke datasets de resultaten van de masterproef
(hoofdstuk 5) dragen, welke data zijn uitgesloten en waarom, en met welke
firmware en code alles is gemeten. Bijlage C van de thesis is de compacte
versie van dit document.

## Negenbordencampagne (15 augustus 2026)

| sessiemap | borden | gestart | afgerond |
|---|---|---|---|
| `metingen/campagne_20260815_054820` | DK1, DK2, DK3, F24A, F24C, F26A, F26B, F26C | 05:48:25 | 08:31:34 |
| `metingen/campagne_20260815_091825` | F24B (nagemeten, zie uitsluitingen) | 09:18:30 | 09:39:03 |

Inhoud samen: 9 borden × (5 runs ronde 04 + 5 runs ronde 07) = 45 runs,
315 meetrijen en 36 450 checkpointrijen, zonder één ongeldige rij;
45/45 zelftests geslaagd (telkens exact één detectie, in segment 5, met
verdict `LS_FOUT_CRCFINAL`).

**Firmware (bit-identiek in beide sessiemanifesten):**

| binary | sha256 |
|---|---|
| `coremark_baseline.bin` (ronde 04) | `867B44EDFDC8BA85745A18D78F50ECCCAC31C94EEEAAEBC9C3D05BB4FCC3B580` |
| `coremark_lockstep.bin` (ronde 07) | `1DB0948FE74BCE25A66DC6B721DFBFD1E37ED20A6BC99AC5264F83F08B3D0845` |

Gebouwd met ESP-IDF v5.5 op broncommit `ba9c122`; meetomgeving
Python 3.11.9. De meetcode van de campagne staat in `meetsuite/`
(`meet_alle_borden.ps1`, `meet_bord.py`, `borden.csv`).

**Logverificatie:** achttien logbestanden onafhankelijk gehasht, 18/18
gelijk aan `log_sha256` in de CSV's; een externe controle bevestigde alle
90 logverwijzingen tegen CSV én manifest. Kanttekening: `meet_bord.py`
schrijft de seriële uitvoer in tekstmodus (CR CR LF waar de chip CR LF
stuurde); de hash bewijst het gearchiveerde tekstbestand, niet de
oorspronkelijke bytevolgorde.

## Uitsluitingen, met reden

1. **Drie afgebroken F24B-pogingen** (06:14, 07:36, 09:02 — de laatste
   op COM21): telkens ~5 s na aanvang gefaald met `FileNotFoundError` bij
   het openen van de seriële poort, dus vóór enige meting. Na verplaatsing
   naar een USB-poort aan de achterzijde slaagde de meting onmiddellijk.
   Poort- of USB-padgebonden probleem; precieze oorzaak niet vastgesteld.
   Er is niet uit meerdere geldige resultaten gekozen: er bestonden geen
   eerdere geldige F24B-resultaten.
2. **Vier geldige DK1-runs uit de afgebroken campagne van 04:59** —
   uitgesloten om selectie-effecten uit te sluiten; DK1 is volledig
   opnieuw gemeten in de hoofdsessie.

**Effect van opname F24B:** het hoofdcijfer (mediaan C→D) verschuift met
−0,0002 procentpunt; alle vergelijkingen zijn stabiel.

**`git_schoon: false` in de sessiemanifesten is betekenisloos:** het
meetscript maakt de sessiemap in de repository aan vóór de
git-statuscontrole, en `metingen/` staat niet in `.gitignore`. Nagekeken:
`git status --porcelain` toonde uitsluitend `??`-vermeldingen voor
meetmappen en `git diff --stat HEAD` was leeg — geen enkel gevolgd
bronbestand week af van `ba9c122`.

## Overige dragende datasets

- **Basissessies 8–9 augustus 2026:** ruwe logs (datum 20260808/20260809),
  CSV's en analyse-uitvoer per ronde in de projectmappen onder
  `2026_hardwarevalidatie/`.
- **Ronde 06 — gerichte foutinjectie (8 augustus 2026):** 333 injecties;
  CSV sha256 `3bc0054a6ef4b16697b201f4572b6c2e7b0d2b5657f4a0c4de07ae1a328a3110`,
  serieel log sha256
  `f0d15e4f0d50d2615fab7a71c0b417e9bfbbe769496945df194fcfe169ae42b9`.
- **Rondes 03 en 05, v2-hermetingen (15 augustus 2026):** logs met datum
  20260815 in `03_pingpong_oneway`, `03_pingpong_oneway_rtos` en
  `05_lockstep_kern`. De v1-resultaten van ronde 05 zijn ongeldig verklaard
  (weggeoptimaliseerde referentielus, bewezen in de disassembly) en alleen
  de v2-cijfers dragen resultaten.
- **Externe injectie (10 augustus 2026):** twaalf debugger-gemedieerde
  injecties over drie borden; auditlogs in `05b_jtag_externe_injectie`.

## Bekende beperkingen van de meetketen (bewust niet "gerepareerd")

- De iteratiecontrole `iteraties == segmenten × seg_iters` is tautologisch
  (de firmware berekent dat veld); de kruischeck met ronde 04 (≤ 0,0002 %)
  maakt correcte telling aannemelijk, meer niet.
- `audit_inject_v3.ps1` (JTAG-ronde) bevat harde paden naar een oudere
  mapstructuur en sluit niet af met een exitcode op `$passAll`; de
  negen-borden-herhaling van de JTAG-ronde is buiten scope verklaard. Het
  3-bordenresultaat van 10 augustus draagt de claim in de thesis.
- Herstel van beide punten vergt een firmware- respectievelijk
  meetcodewijziging en dus een nieuwe campagne; de firmware van rondes 04
  en 07 is bevroren omdat elke wijziging de gevalideerde dataset ongeldig
  zou maken.
