# Verantwoording AI-gebruik

Ik gebruik generatieve AI in dit traject openlijk als hulpmiddel, en dit
document legt vast hoe — conform de KU Leuven GenAI-gedragscode voor
studenten en artikel 84 van het onderwijs- en examenreglement (het werk moet
mijn eigen kennis, inzicht en vaardigheden weerspiegelen). Bij de thesis
dien ik het bijbehorende GenAI-verklaringsformulier van de faculteit in, en
de thesis bevat een transparantieverklaring als bijlage; mijn promotor is
per mail geïnformeerd dat AI transparant als hulpmiddel wordt ingezet. Dit
gebruik is er mede door mijn les van 2025: toen heb ik AI-gegenereerde code
en resultaten onvoldoende gevalideerd, en dat werk heb ik zelf als
niet-verdedigbaar bestempeld. De werkwijze hieronder is ingevoerd om die
fout te vermijden.

## Welke tools, waarvoor

**Tools:** Claude (Anthropic), gebruikt in werksessies via Claude Code en
Cowork; ChatGPT (OpenAI), onder meer voor kritische reviews van tekst en
data; OpenAI Codex, voor onafhankelijke code- en documentreviews; en
Perplexity, voor een onafhankelijke review van de thesistekst zonder
voorkennis van het traject (augustus 2026).

**Gebruikscategorieën** volgens het faculteitsformulier: genereren van
programmeercode (testprogramma's, instrumentatie, meet- en buildscripts),
hulp bij het analyseren van meetdata en broncode (o.a. de
ESP-IDF-implementatie van de task-watchdog en de Xtensa-disassembly van
ronde 5), AI-ondersteunde formulering en revisie van de thesistekst en de
projectdocumentatie, en kritische review van tussenversies. AI wordt niet
gebruikt om resultaten, metingen of conclusies te verzinnen. De thesistekst
is tot stand gekomen met AI-assistentie op basis van mijn metingen,
structuurkeuzes en aanwijzingen; ik lees elke passage na, corrigeer waar
nodig en draag er de volledige inhoudelijke verantwoordelijkheid voor.

## Rolverdeling en validatie

De onderzoeksvragen, de scope-afbakening, de meetopzet en elke go/no-go-
beslissing zijn van mij; AI fungeert als versneller bij het uitschrijven van
code die ik specificeer, als redacteur bij het uitschrijven van tekst op
basis van mijn resultaten, en als kritische sparringpartner bij het duiden
van metingen. Voor elke AI-geassisteerde bijdrage geldt dezelfde toets: ik
lees de code na, bouw ze, flash ze op het echte bord en aanvaard alleen wat
een seriële log of meting bevestigt. Alle cijfers in deze repository komen
uit runs op mijn eigen hardware, met de ruwe logs als bewijs naast de code.
Claims die AI aandraagt, worden tegen primaire bronnen gecontroleerd
(bijvoorbeeld: de verklaring van het watchdog-effect is geverifieerd in de
ESP-IDF v5.5-broncode en in mijn eigen log, de CoreMark-bronnen zijn per
MD5-hash vergeleken met de officiële EEMBC-repository, en
literatuurgegevens zijn via Crossref en uitgeversrecords geverifieerd) —
niet op gezag van het model aanvaard. Dat geldt ook voor AI-reviews van dit
werk zelf: elk reviewpunt is eerst tegen de broncode of de ruwe data
geverifieerd vóór het al dan niet werd verwerkt, omdat de reviews zowel
terechte als aantoonbaar onterechte punten bevatten.

## Auteurschap en herkenbaarheid

Alle commits in deze repository staan uitsluitend op mijn naam, en dat is
een bewuste keuze: een auteur moet verantwoordelijkheid kunnen dragen voor
het werk, en een AI-model kan dat niet. AI is hier een ondersteunend
hulpmiddel; de inhoudelijke verantwoordelijkheid, de validatie en het
auteurschap blijven bij mij. De transparantie over AI-gebruik staat op de
juiste plaats: in dit document, in het logboek (dat per werksessie
beschrijft wat met AI-assistentie gebeurde) en in de Code of Conduct
GenAI-transparantieverklaring die als bijlage bij de masterproef hoort.
Ik valideer elke wijziging op de hardware en voer de commits zelf uit. In
de thesistekst verwijs ik naar AI-gebruik volgens de geldende
KU Leuven-richtlijnen voor refereren naar GenAI.
