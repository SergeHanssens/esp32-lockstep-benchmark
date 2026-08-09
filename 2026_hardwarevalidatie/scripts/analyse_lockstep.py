# analyse_lockstep.py — analyse van 07-meetruns volgens het statistiekplan:
#   * over de runscores per fase: gemiddelde + stddev + min/max (n=10 is te
#     weinig voor percentielen over runs);
#   * over de per-checkpoint-latenties BINNEN de runs (gepoold, duizenden
#     datapunten): P50/P99/P99,9 + max (nearest-rank op de gesorteerde lijst);
#   * overhead = relatieve score-daling (%) beschermd t.o.v. onbeschermd.
# Gebruik: python analyse_lockstep.py lockstep_fasen_X.csv lockstep_checkpoints_X.csv
# GEEN verzonnen data: alles komt uit de CSV's van meet_lockstep_runs.py.
import sys, csv, math
from collections import defaultdict

CPU_MHZ = 240.0

def lees(pad):
    with open(pad, newline="", encoding="utf-8") as f:
        return [r for r in csv.DictReader(f)]

def gem_std(v):
    n = len(v)
    g = sum(v) / n
    s = math.sqrt(sum((x - g) ** 2 for x in v) / (n - 1)) if n > 1 else 0.0
    return g, s

def perc(gesorteerd, p):
    """nearest-rank percentiel op een reeds gesorteerde lijst"""
    n = len(gesorteerd)
    k = max(1, math.ceil(p / 100.0 * n))
    return gesorteerd[k - 1]

fasen = lees(sys.argv[1])
chk = lees(sys.argv[2]) if len(sys.argv) > 2 else []
fasen = [r for r in fasen if r.get("fase") not in (None, "", "ONGELDIG")]

# --- runscores per fase ----------------------------------------------------
per_fase = defaultdict(lambda: {"getimed": [], "eff": [], "mism": 0, "zt": []})
for r in fasen:
    d = per_fase[r["fase"]]
    d["getimed"].append(float(r["score_getimed"]))
    d["eff"].append(float(r["score_effectief"]))
    d["mism"] += int(r["mismatches"] or 0)
    if r.get("zelftest") and r["zelftest"] != "nvt":
        d["zt"].append(r["zelftest"])

print("=== Runscores per fase (n = aantal runs) ===")
for naam, d in per_fase.items():
    for label, v in (("getimed", d["getimed"]), ("effectief", d["eff"])):
        g, s = gem_std(v)
        print(f"{naam:22s} {label:9s} n={len(v):2d}  gem {g:8.2f}  stddev {s:7.4f}  "
              f"min {min(v):8.2f}  max {max(v):8.2f} it/s")
    print(f"{naam:22s} mismatches totaal: {d['mism']}"
          + (f"  zelftest: {','.join(sorted(set(d['zt'])))}" if d["zt"] else ""))

def overhead(basis, doel, label):
    if basis not in per_fase or doel not in per_fase:
        return
    for veld, txt in (("getimed", "getimed"), ("eff", "effectief")):
        gb, _ = gem_std(per_fase[basis][veld])
        gd, _ = gem_std(per_fase[doel][veld])
        print(f"{label:52s} [{txt:9s}] {100.0 * (gb - gd) / gb:6.2f}% "
              f"({gb:.2f} -> {gd:.2f} it/s)")

print("\n=== Overhead = relatieve score-daling t.o.v. referentie ===")
overhead("onbeschermd_vol", "beschermd_duaal_vol", "duale uitvoering (buscontentie), vol")
overhead("onbeschermd_vol", "onbeschermd_segment", "segmentatie alleen")
overhead("onbeschermd_segment", "beschermd_segment", "checkpoint-lockstep bovenop segmentatie")
overhead("onbeschermd_vol", "beschermd_segment", "checkpoint-lockstep TOTAAL (kerncijfer)")

# --- checkpointlatenties (gepoold over alle runs, binnen-run-datapunten) ---
if chk:
    print("\n=== Checkpointlatenties per fase (gepoold over runs) ===")
    lat_per_fase = defaultdict(list)
    for r in chk:
        if r.get("fase") and r.get("latentie_cyc"):
            lat_per_fase[r["fase"]].append(int(r["latentie_cyc"]))
    for naam, lat in lat_per_fase.items():
        lat = [x for x in lat if x > 0]
        if not lat:
            continue
        lat.sort()
        g, s = gem_std([float(x) for x in lat])
        p50, p99, p999 = perc(lat, 50), perc(lat, 99), perc(lat, 99.9)
        print(f"{naam:22s} n={len(lat):5d}  gem {g:9.1f}  P50 {p50:7d}  "
              f"P99 {p99:7d}  P99,9 {p999:7d}  max {lat[-1]:7d} cycli")
        print(f"{'':22s} (in us @ {CPU_MHZ:.0f} MHz: gem {g/CPU_MHZ:6.2f}  "
              f"P50 {p50/CPU_MHZ:6.2f}  P99 {p99/CPU_MHZ:6.2f}  "
              f"P99,9 {p999/CPU_MHZ:6.2f}  max {lat[-1]/CPU_MHZ:6.2f})")
    # verdicts buiten de zelftestfase moeten 0 zijn
    fout = [r for r in chk if r.get("verdict") not in ("0", "", None)
            and r.get("fase") != "zelftest_segment"]
    print(f"\nverdicts != 0 buiten zelftestfase: {len(fout)} (verwacht 0)")
    zt = [r for r in chk if r.get("fase") == "zelftest_segment"
          and r.get("verdict") not in ("0", "", None)]
    print(f"verdicts != 0 in zelftestfase: {len(zt)} (verwacht 1 per run, seg 5, verdict 1)")
print("\n=== Analyse klaar. Cijfers rechtstreeks uit de meet-CSV's. ===")
