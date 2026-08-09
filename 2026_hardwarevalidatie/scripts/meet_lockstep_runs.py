# meet_lockstep_runs.py — 07_coremark_lockstep: n runs capturen naar twee CSV's.
# Gebruik: python meet_lockstep_runs.py COM3 10 [max_sec_per_run]
# Vereist: 07_coremark_lockstep-firmware reeds geflasht op het bord.
# Elke run = harde reset via RTS -> 5 fasen -> ruwe log bewaard ->
#   FASE07-regels  -> lockstep_fasen_<datum>.csv       (1 rij per fase per run)
#   CSV07-regels   -> lockstep_checkpoints_<datum>.csv (1 rij per segment)
# GEEN verzonnen data: alles komt uit de seriële capture; mislukte parse => rij 'ONGELDIG'.
import sys, time, os, csv
from datetime import datetime
import serial

port = sys.argv[1] if len(sys.argv) > 1 else "COM3"
n_runs = int(sys.argv[2]) if len(sys.argv) > 2 else 10
max_sec = float(sys.argv[3]) if len(sys.argv) > 3 else 220.0

stamp_dag = datetime.now().strftime("%Y%m%d")
logdir = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                      "lockstep_runs_" + stamp_dag)
os.makedirs(logdir, exist_ok=True)
csv_fasen = os.path.join(logdir, "lockstep_fasen_" + stamp_dag + ".csv")
csv_chk = os.path.join(logdir, "lockstep_checkpoints_" + stamp_dag + ".csv")

def capture_run(poort, maxsec):
    """Reset het bord via RTS en lees tot '=== KLAAR' of timeout."""
    s = serial.Serial(poort, 115200, timeout=1)
    s.dtr = False
    s.rts = True
    time.sleep(0.1)
    s.rts = False
    data = b""
    t0 = time.time()
    while time.time() - t0 < maxsec:
        chunk = s.read(4096)
        if chunk:
            data += chunk
            if b"=== KLAAR" in data:
                time.sleep(0.5)
                data += s.read(8192)
                break
    s.close()
    return data.decode("utf-8", errors="replace")

V_FASEN = ["datum_iso", "run", "fase", "contexts", "segmenten", "seg_iters",
           "iteraties", "som_ticks_us", "wall_us", "score_getimed",
           "score_effectief", "error_regels", "mismatches", "zelftest",
           "gevalideerd_vol", "logbestand"]
V_CHK = ["datum_iso", "run", "fase", "seg", "ticks_us", "latentie_cyc", "verdict"]

def parse_fase_regels(tekst):
    """FASE07;naam;ctx;segs;seg_iters;iters;som_ticks;wall;sc_get;sc_eff;err;mism;zt"""
    rijen = []
    for regel in tekst.splitlines():
        if not regel.startswith("FASE07;"):
            continue
        d = regel.strip().split(";")
        if len(d) != 13:
            continue
        rijen.append({"fase": d[1], "contexts": d[2], "segmenten": d[3],
                      "seg_iters": d[4], "iteraties": d[5], "som_ticks_us": d[6],
                      "wall_us": d[7], "score_getimed": d[8],
                      "score_effectief": d[9], "error_regels": d[10],
                      "mismatches": d[11], "zelftest": d[12]})
    return rijen

def parse_chk_regels(tekst):
    """CSV07;fase;seg;ticks_us;latentie_cyc;verdict"""
    rijen = []
    for regel in tekst.splitlines():
        if not regel.startswith("CSV07;"):
            continue
        d = regel.strip().split(";")
        if len(d) != 6:
            continue
        rijen.append({"fase": d[1], "seg": d[2], "ticks_us": d[3],
                      "latentie_cyc": d[4], "verdict": d[5]})
    return rijen

nieuw_f = not os.path.exists(csv_fasen)
nieuw_c = not os.path.exists(csv_chk)
ff = open(csv_fasen, "a", newline="", encoding="utf-8")
fc = open(csv_chk, "a", newline="", encoding="utf-8")
wf = csv.DictWriter(ff, fieldnames=V_FASEN)
wc = csv.DictWriter(fc, fieldnames=V_CHK)
if nieuw_f:
    wf.writeheader()
if nieuw_c:
    wc.writeheader()

for run in range(1, n_runs + 1):
    t_start = datetime.now().isoformat(timespec="seconds")
    print(f"=== RUN {run}/{n_runs} gestart {t_start} (max {max_sec:.0f}s) ===", flush=True)
    tekst = capture_run(port, max_sec)
    logpad = os.path.join(logdir, f"run{run:02d}_{datetime.now().strftime('%H%M%S')}.txt")
    with open(logpad, "w", encoding="utf-8") as lf:
        lf.write(tekst)
    fasen = parse_fase_regels(tekst)
    # extra kruischeck: aantal 'Correct operation validated' in de volle fasen (A+B)
    n_valid = tekst.count("Correct operation validated")
    if not fasen:
        wf.writerow({"datum_iso": t_start, "run": run, "fase": "ONGELDIG",
                     "logbestand": os.path.basename(logpad)})
        ff.flush()
        print(f"  !! geen FASE07-regels geparst, zie {logpad}", flush=True)
        continue
    for r in fasen:
        r.update({"datum_iso": t_start, "run": run, "gevalideerd_vol": n_valid,
                  "logbestand": os.path.basename(logpad)})
        wf.writerow(r)
        print(f"  {r['fase']}: getimed {r['score_getimed']} it/s, eff {r['score_effectief']} it/s, "
              f"mismatches {r['mismatches']}, zelftest {r['zelftest']}", flush=True)
    for r in parse_chk_regels(tekst):
        r.update({"datum_iso": t_start, "run": run})
        wc.writerow(r)
    ff.flush(); fc.flush()

ff.close(); fc.close()
print(f"=== KLAAR: fasen in {csv_fasen} ===")
print(f"=== KLAAR: checkpoints in {csv_chk} ===")
