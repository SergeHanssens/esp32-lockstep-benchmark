# meet_coremark_runs.py — Checkpoint 1: n CoreMark-runs capturen en naar CSV schrijven.
# Gebruik: python meet_coremark_runs.py COM3 10 [max_sec_per_run]
# Vereist: 04_coremark-firmware (pass A + pass B) reeds geflasht op het bord.
# Elke run = harde reset via RTS (zoals capture_generic.py) -> volledige boot +
# pass A + pass B -> ruwe log bewaard -> beide passes geparst -> rijen in CSV.
# GEEN verzonnen data: alles komt uit de seriële capture; mislukte parse => rij 'ONGELDIG'.
import sys, time, re, os, csv
from datetime import datetime
import serial

port = sys.argv[1] if len(sys.argv) > 1 else "COM3"
n_runs = int(sys.argv[2]) if len(sys.argv) > 2 else 10
max_sec = float(sys.argv[3]) if len(sys.argv) > 3 else 110.0

stamp_dag = datetime.now().strftime("%Y%m%d")
logdir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "coremark_runs_" + stamp_dag)
os.makedirs(logdir, exist_ok=True)
csvpad = os.path.join(logdir, "coremark_runs_" + stamp_dag + ".csv")

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

PASS_RE = re.compile(r"--- PASS ([AB]):")

def parse_passes(tekst):
    """Splits de log op PASS-koppen en parseer per pass de CoreMark-velden."""
    rijen = []
    delen = PASS_RE.split(tekst)
    # delen = [voorloop, 'A', tekst_A, 'B', tekst_B]
    for i in range(1, len(delen) - 1, 2):
        label, blok = delen[i], delen[i + 1]
        def pak(patroon):
            m = re.search(patroon, blok)
            return m.group(1) if m else ""
        rijen.append({
            "pass": label,
            "iterations": pak(r"Iterations\s*:\s*(\d+)"),
            "total_ticks": pak(r"Total ticks\s*:\s*(\d+)"),
            "iterations_per_sec": pak(r"Iterations/Sec\s*:\s*([\d.]+)"),
            "crcfinal": pak(r"\[0\]crcfinal\s*:\s*(0x[0-9a-fA-F]+)"),
            "gevalideerd": "ja" if "Correct operation validated" in blok else "NEE",
            "score_regel": pak(r"(CoreMark 1\.0 : [^\r\n]+)"),
        })
    return rijen

nieuw = not os.path.exists(csvpad)
velden = ["datum_iso", "run", "pass", "iterations", "total_ticks",
          "iterations_per_sec", "crcfinal", "gevalideerd", "score_regel", "logbestand"]
with open(csvpad, "a", newline="", encoding="utf-8") as f:
    w = csv.DictWriter(f, fieldnames=velden)
    if nieuw:
        w.writeheader()
    for run in range(1, n_runs + 1):
        t_start = datetime.now().isoformat(timespec="seconds")
        print(f"=== RUN {run}/{n_runs} gestart {t_start} (max {max_sec:.0f}s) ===", flush=True)
        tekst = capture_run(port, max_sec)
        logpad = os.path.join(logdir, f"run{run:02d}_{datetime.now().strftime('%H%M%S')}.txt")
        with open(logpad, "w", encoding="utf-8") as lf:
            lf.write(tekst)
        rijen = parse_passes(tekst)
        if not rijen:
            w.writerow({"datum_iso": t_start, "run": run, "pass": "ONGELDIG",
                        "gevalideerd": "NEE", "logbestand": os.path.basename(logpad)})
            print(f"  !! geen CoreMark-resultaat geparst, zie {logpad}", flush=True)
            continue
        for r in rijen:
            r.update({"datum_iso": t_start, "run": run, "logbestand": os.path.basename(logpad)})
            w.writerow(r)
            print(f"  pass {r['pass']}: {r['iterations_per_sec']} it/s  crcfinal {r['crcfinal']}  gevalideerd={r['gevalideerd']}", flush=True)
        f.flush()
print(f"=== KLAAR: resultaten in {csvpad} ===")
