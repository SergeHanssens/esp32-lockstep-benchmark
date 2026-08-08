# parse_foutinjectie.py — haalt de INJ-regels (campagne A) uit een 06-log en
# schrijft ze als CSV. Gebruik: python parse_foutinjectie.py <log> <csv>
# Geen verzonnen data: regels die niet parsen worden gerapporteerd, niet gemaakt.
import sys, csv

logpad, csvpad = sys.argv[1], sys.argv[2]
with open(logpad, encoding="utf-8", errors="replace") as f:
    regels = f.read().splitlines()

rijen, fouten = [], 0
binnen = False
for r in regels:
    if r.strip() == "CSV_START":
        binnen = True; continue
    if r.strip() == "CSV_EIND":
        binnen = False; continue
    if binnen and r.startswith("INJ;"):
        d = r.split(";")
        if d[1] == "ronde":
            continue          # kopregel van de firmware zelf, geen datarij
        if len(d) == 7:
            rijen.append(d[1:])
        else:
            fouten += 1

with open(csvpad, "w", newline="", encoding="utf-8") as f:
    w = csv.writer(f)
    w.writerow(["ronde", "doelwit", "woord", "bit", "verdict", "latentie_cycli"])
    w.writerows(rijen)

print(f"{len(rijen)} injecties naar {csvpad} geschreven ({fouten} niet-parseerbare regels)")
