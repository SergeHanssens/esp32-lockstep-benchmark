#!/usr/bin/env python3
# meet_bord.py — bordbewuste meetcapture voor de masterproef (softwarelockstep ESP32-S3).
#
# Vervangt meet_coremark_runs.py en meet_lockstep_runs.py. Verschil met die twee:
#   1. elke CSV-rij draagt de bordidentiteit (adapterserienummer, bordtype, naam);
#      de oude scripts schreven negen borden door elkaar in één dagbestand;
#   2. een run is pas GELDIG als de verwachte structuur volledig aanwezig is;
#      een afgekapte capture wordt niet stilzwijgend half geparst;
#   3. van elke ruwe log wordt de SHA-256 bewaard, plus een manifest per meetsessie,
#      zodat elk getal in de thesis herleidbaar is naar een ondertekend logbestand.
#
# Gebruik:
#   python meet_bord.py --soort 04 --poort COM1 --serie CC:BA:97:14:2B:28 \
#          --bordtype devkitc1 --bordnaam DK1 --runs 5 --uitmap D:\...\metingen\sessie
#   python meet_bord.py --soort 07 ...    (idem, andere firmware)
#
# Er wordt niets verzonnen: alles komt uit de seriële capture. Mislukte of
# onvolledige runs komen in de CSV met geldig=NEE en een reden.

import argparse
import csv
import hashlib
import json
import os
import re
import sys
import time
from datetime import datetime, timezone

try:
    import serial
except ImportError:
    sys.exit("pyserial ontbreekt: pip install pyserial")

BAUD = 115200

# Merker die bewijst dat de reset echt gebeurd is (eerste-fase bootloader in ROM).
BOOT_MERKERS = ("ESP-ROM:esp32s3", "ESP-ROM:esp32")
KLAAR_MERKER = "=== KLAAR"


# --------------------------------------------------------------------------
# seriele capture
# --------------------------------------------------------------------------

def open_poort(poort, pogingen=8, wacht=2.0):
    """Opent de poort met hertries: na een flash geeft Windows hem niet meteen vrij."""
    laatste = None
    for _ in range(pogingen):
        try:
            return serial.Serial(poort, BAUD, timeout=1)
        except Exception as e:
            laatste = e
            time.sleep(wacht)
    raise laatste


def capture_run(poort, max_sec, stil_sec=None):
    """Harde reset via RTS, dan lezen tot KLAAR-merker of timeout.

    De capture synchroniseert op de ROM-bootmerker: alles wat vóór de eerste
    merker binnenkomt wordt weggegooid. Zonder die synchronisatie kan een staart
    van een vorige run (die ook op '=== KLAAR' eindigt) deze run voortijdig
    afsluiten en als geldig laten doorgaan.

    Retourneert (tekst, reden). reden is '' bij een normale KLAAR-afsluiting.
    """
    s = open_poort(poort)
    try:
        # eerst leegtrekken tot het stil is: restanten van een vorige run weg
        s.reset_input_buffer()
        s.reset_output_buffer()
        t_drain = time.time()
        while time.time() - t_drain < 3.0:
            if not s.read(4096):
                break

        # auto-resetsequentie; timing zoals esptool voor USB-Serial-JTAG
        s.dtr = False
        s.rts = True
        time.sleep(0.2)
        s.rts = False
        time.sleep(0.2)

        rauw = b""
        sync = -1                      # index van de eerste ROM-merker
        t0 = time.time()
        t_laatste = t0
        reden = "timeout zonder KLAAR-merker"
        while time.time() - t0 < max_sec:
            chunk = s.read(4096)
            if chunk:
                rauw += chunk
                t_laatste = time.time()
                if sync < 0:
                    for m in BOOT_MERKERS:
                        i = rauw.find(m.encode())
                        if i >= 0:
                            sync = i
                            break
                if sync >= 0 and KLAAR_MERKER.encode() in rauw[sync:]:
                    time.sleep(0.5)
                    rauw += s.read(1 << 20)
                    reden = ""
                    break
            elif stil_sec is not None and (time.time() - t_laatste) > stil_sec and rauw:
                reden = "bord %.0fs stil na laatste uitvoer" % stil_sec
                break
    finally:
        s.close()

    if sync < 0:
        # niets weggooien: de volledige capture blijft bewaard als bewijs
        return rauw.decode("utf-8", errors="replace"), \
               (reden or "geen ROM-bootmerker: reset niet aantoonbaar")
    return rauw[sync:].decode("utf-8", errors="replace"), reden


# --------------------------------------------------------------------------
# parsers
# --------------------------------------------------------------------------

PASS_RE = re.compile(r"--- PASS ([AB]):")


def parse_04(tekst):
    """04_coremark: pass A en pass B. Retourneert (rijen, reden_ongeldig)."""
    rijen = []
    delen = PASS_RE.split(tekst)
    for i in range(1, len(delen) - 1, 2):
        label, blok = delen[i], delen[i + 1]

        def pak(patroon):
            m = re.search(patroon, blok)
            return m.group(1) if m else ""

        rijen.append({
            "pass_of_fase": label,
            "iteraties": pak(r"Iterations\s*:\s*(\d+)"),
            "total_ticks": pak(r"Total ticks\s*:\s*(\d+)"),
            "score_it_per_s": pak(r"Iterations/Sec\s*:\s*([\d.]+)"),
            "crcfinal": pak(r"\[0\]crcfinal\s*:\s*(0x[0-9a-fA-F]+)"),
            "gevalideerd": "ja" if "Correct operation validated" in blok else "NEE",
            "score_regel": pak(r"(CoreMark 1\.0 : [^\r\n]+)"),
        })

    if len(rijen) != 2:
        return rijen, "verwacht 2 passes (A en B), gevonden %d" % len(rijen)
    for r in rijen:
        if not r["score_it_per_s"]:
            return rijen, "pass %s zonder Iterations/Sec" % r["pass_of_fase"]
        if r["gevalideerd"] != "ja":
            return rijen, "pass %s niet gevalideerd door CoreMark zelf" % r["pass_of_fase"]
    return rijen, ""


# naam, contexts, segmenten, seg_iters -- exact zoals FASEN[] in lockstep_wrapper.c
VERWACHTE_FASEN = [
    ("onbeschermd_vol",     1,   1, 20000),
    ("beschermd_duaal_vol", 2,   1, 20000),
    ("onbeschermd_segment", 1, 400,    50),
    ("beschermd_segment",   2, 400,    50),
    ("zelftest_segment",    2,  10,    50),
]
ZELFTEST_SEGMENT = 5                 # ls_selftest_segment in lockstep_wrapper.c
LS_FOUT_CRCFINAL = 1                 # (1u << 0) in core_portme.h


def parse_07_fasen(tekst):
    """FASE07;naam;ctx;segs;seg_iters;iters;som_ticks;wall;sc_get;sc_eff;err;mism;zt"""
    rijen = []
    for regel in tekst.splitlines():
        if not regel.startswith("FASE07;"):
            continue
        d = regel.strip().split(";")
        if len(d) != 13:
            continue
        rijen.append({
            "pass_of_fase": d[1], "contexts": d[2], "segmenten": d[3],
            "seg_iters": d[4], "iteraties": d[5], "som_ticks_us": d[6],
            "wall_us": d[7], "score_it_per_s": d[8], "score_effectief": d[9],
            "error_regels": d[10], "mismatches": d[11], "zelftest": d[12],
        })
    return rijen


def parse_07_checkpoints(tekst):
    """CSV07;fase;seg;ticks_us;latentie_cyc;verdict"""
    rijen = []
    for regel in tekst.splitlines():
        if not regel.startswith("CSV07;"):
            continue
        d = regel.strip().split(";")
        if len(d) != 6:
            continue
        rijen.append({"pass_of_fase": d[1], "seg": d[2], "ticks_us": d[3],
                      "latentie_cyc": d[4], "verdict": d[5]})
    return rijen


def keur_07(fasen, chk):
    """Structurele keuring van een 07-run. Retourneert reden of '' als geldig.

    De keuring vertrouwt niet op de samenvattingsregels alleen: het aantal
    mismatches en het zelftestoordeel worden opnieuw berekend uit de ruwe
    CSV07-regels. Wijken firmware-samenvatting en ruwe reeks af, dan is de run
    ongeldig -- juist dat verschil zou anders onopgemerkt blijven.
    """
    if len(fasen) != len(VERWACHTE_FASEN):
        return "verwacht %d fasen, gevonden %d" % (len(VERWACHTE_FASEN), len(fasen))

    for r, (naam, ctx, segs, iters) in zip(fasen, VERWACHTE_FASEN):
        if r["pass_of_fase"] != naam:
            return "fasenvolgorde wijkt af: %s waar %s verwacht werd" % (
                r["pass_of_fase"], naam)
        if (int(r["contexts"]), int(r["segmenten"]), int(r["seg_iters"])) != (ctx, segs, iters):
            return "fase %s: parameters (%s,%s,%s) wijken af van (%d,%d,%d)" % (
                naam, r["contexts"], r["segmenten"], r["seg_iters"], ctx, segs, iters)
        if int(r["iteraties"]) != segs * iters:
            # Deze controle kan de uitvoering niet valideren -- de firmware
            # berekent iteraties als segmenten*seg_iters. Hij valideert wel de
            # interne samenhang van de verzonden regel: een verminkte seriele
            # regel valt hierdoor door de mand.
            return "fase %s: iteraties %s != segmenten*seg_iters %d" % (
                naam, r["iteraties"], segs * iters)
        if int(r["error_regels"]) != 0:
            return "fase %s: %s ERROR-regel(s) in CoreMark-uitvoer" % (naam, r["error_regels"])

    # per gesegmenteerde fase: de checkpointreeks moet compleet en uniek zijn
    for naam, ctx, segs, iters in VERWACHTE_FASEN:
        if segs <= 1:
            continue
        gezien = [c for c in chk if c["pass_of_fase"] == naam]
        if len(gezien) != segs:
            return "fase %s: %d CSV07-regels, verwacht %d" % (naam, len(gezien), segs)
        nummers = [int(c["seg"]) for c in gezien]
        if sorted(nummers) != list(range(segs)):
            return "fase %s: segmentnummers niet 0..%d uniek (dubbel of ontbrekend)" % (
                naam, segs - 1)

    # mismatches hertellen uit de ruwe reeks en vergelijken met de samenvatting
    for r in fasen:
        naam = r["pass_of_fase"]
        gezien = [c for c in chk if c["pass_of_fase"] == naam]
        if not gezien:
            continue                       # niet-gesegmenteerde fase logt geen CSV07
        herteld = sum(1 for c in gezien if int(c["verdict"]) != 0)
        if herteld != int(r["mismatches"]):
            return "fase %s: samenvatting meldt %s mismatches, ruwe reeks telt %d" % (
                naam, r["mismatches"], herteld)

    # zonder injectie hoort geen enkele fase een mismatch te geven
    for r in fasen:
        if r["pass_of_fase"] != "zelftest_segment" and int(r["mismatches"]) != 0:
            return "fase %s: %s mismatch(es) zonder injectie" % (
                r["pass_of_fase"], r["mismatches"])

    # zelftest: onafhankelijk narekenen, niet alleen het firmware-oordeel geloven
    zt = [r for r in fasen if r["pass_of_fase"] == "zelftest_segment"][0]
    if zt["zelftest"] != "GESLAAGD":
        return "zelftest niet geslaagd (%s): detectieketen onbewezen op dit bord" % zt["zelftest"]
    ztc = [c for c in chk if c["pass_of_fase"] == "zelftest_segment"]
    fout = [c for c in ztc if int(c["verdict"]) != 0]
    if len(fout) != 1:
        return "zelftest: %d segmenten met niet-nul verdict, verwacht precies 1" % len(fout)
    if int(fout[0]["seg"]) != ZELFTEST_SEGMENT:
        return "zelftest: detectie in segment %s, verwacht %d" % (
            fout[0]["seg"], ZELFTEST_SEGMENT)
    if int(fout[0]["verdict"]) != LS_FOUT_CRCFINAL:
        return "zelftest: verdict %s, verwacht %d (LS_FOUT_CRCFINAL)" % (
            fout[0]["verdict"], LS_FOUT_CRCFINAL)
    return ""


# --------------------------------------------------------------------------
# bewijsketen
# --------------------------------------------------------------------------

def sha256_bestand(pad):
    h = hashlib.sha256()
    with open(pad, "rb") as f:
        for blok in iter(lambda: f.read(65536), b""):
            h.update(blok)
    return h.hexdigest()


def lees_bootcontext(tekst):
    """Haalt uit de bootlog wat het bord over zichzelf zegt.

    Dit maakt de bordidentiteit controleerbaar uit het logbestand zelf en niet
    alleen uit onze eigen tabel.
    """
    ctx = {}
    m = re.search(r"(ESP-ROM:[^\r\n]+)", tekst)
    if m:
        ctx["rom"] = m.group(1).strip()
    m = re.search(r"ESP-IDF[: ]+v?([0-9][^\s\r\n]*)", tekst)
    if m:
        ctx["idf"] = m.group(1).strip()
    m = re.search(r"chip revision:?\s*v?([0-9.]+)", tekst, re.I)
    if m:
        ctx["chip_rev"] = m.group(1)
    m = re.search(r"CPU\s+(\d+)\s*MHz", tekst)
    if m:
        ctx["cpu_mhz"] = m.group(1)
    m = re.search(r"SPI Speed\s*:\s*(\d+MHz)", tekst)
    if m:
        ctx["flash_speed"] = m.group(1)
    return ctx


# --------------------------------------------------------------------------
# hoofdprogramma
# --------------------------------------------------------------------------

V_04 = ["datum_iso", "serie", "bordtype", "bordnaam", "poort", "soort", "run",
        "geldig", "reden", "pass_of_fase", "iteraties", "total_ticks",
        "score_it_per_s", "crcfinal", "gevalideerd", "score_regel",
        "logbestand", "log_sha256"]

V_07F = ["datum_iso", "serie", "bordtype", "bordnaam", "poort", "soort", "run",
         "geldig", "reden", "pass_of_fase", "contexts", "segmenten", "seg_iters",
         "iteraties", "som_ticks_us", "wall_us", "score_it_per_s",
         "score_effectief", "error_regels", "mismatches", "zelftest",
         "logbestand", "log_sha256"]

V_07C = ["datum_iso", "serie", "bordtype", "bordnaam", "poort", "run",
         "geldig", "reden", "pass_of_fase", "seg", "ticks_us", "latentie_cyc",
         "verdict", "logbestand", "log_sha256"]


def main():
    p = argparse.ArgumentParser(description="Bordbewuste meetcapture ESP32-S3-lockstep")
    p.add_argument("--soort", required=True, choices=["04", "07"],
                   help="04 = CoreMark-baseline, 07 = vijffasen lockstepmeting")
    p.add_argument("--poort", required=True)
    p.add_argument("--serie", required=True, help="USB-serienummer van de JTAG-adapter")
    p.add_argument("--bordtype", required=True)
    p.add_argument("--bordnaam", required=True)
    p.add_argument("--runs", type=int, default=5)
    p.add_argument("--uitmap", required=True, help="basismap voor deze meetsessie")
    p.add_argument("--max-sec", type=float, default=None,
                   help="harde timeout per run (standaard 150 voor 04, 420 voor 07)")
    p.add_argument("--stil-sec", type=float, default=120.0,
                   help="afbreken als het bord zo lang niets stuurt na eerdere uitvoer. "
                        "Fase B en D zijn ruim 43 s volledig stil (gemeten op DK1, "
                        "8 aug), dus lager dan circa 90 kapt geldige runs af")
    a = p.parse_args()

    if a.runs < 1:
        p.error("--runs moet minstens 1 zijn: nul runs is geen geslaagde meting")

    max_sec = a.max_sec if a.max_sec else (150.0 if a.soort == "04" else 420.0)

    bordmap = os.path.join(a.uitmap, "%s_%s" % (a.bordnaam, a.serie.replace(":", "")))
    logmap = os.path.join(bordmap, "logs_%s" % a.soort)
    os.makedirs(logmap, exist_ok=True)

    if os.path.exists(os.path.join(bordmap, "meting_%s.csv" % a.soort)):
        print("meting_%s.csv bestaat al in %s: hergebruik zou runnummers "
              "dupliceren en het manifest overschrijven. Kies een andere "
              "--uitmap of verwijder de bestaande map." % (a.soort, bordmap),
              file=sys.stderr)
        sys.exit(3)                    # 3 = mag niet starten, niet 1 = niets geldig

    csv_hoofd = os.path.join(bordmap, "meting_%s.csv" % a.soort)
    csv_chk = os.path.join(bordmap, "checkpoints_07.csv")

    velden = V_04 if a.soort == "04" else V_07F
    nieuw = not os.path.exists(csv_hoofd)
    fh = open(csv_hoofd, "a", newline="", encoding="utf-8")
    wh = csv.DictWriter(fh, fieldnames=velden)
    if nieuw:
        wh.writeheader()

    wc = fc = None
    if a.soort == "07":
        nieuw_c = not os.path.exists(csv_chk)
        fc = open(csv_chk, "a", newline="", encoding="utf-8")
        wc = csv.DictWriter(fc, fieldnames=V_07C)
        if nieuw_c:
            wc.writeheader()

    manifest = {
        "bord": {"serie": a.serie, "bordtype": a.bordtype,
                 "bordnaam": a.bordnaam, "poort": a.poort},
        "soort": a.soort,
        "runs_gevraagd": a.runs,
        "start_utc": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "max_sec": max_sec,
        "runs": [],
    }

    n_geldig = 0
    for run in range(1, a.runs + 1):
        t_start = datetime.now().isoformat(timespec="seconds")
        print("=== %s %s run %d/%d op %s (%s) ==="
              % (a.bordnaam, a.soort, run, a.runs, a.poort, t_start), flush=True)

        t0 = time.time()
        try:
            tekst, reden = capture_run(a.poort, max_sec, a.stil_sec)
        except Exception as e:                      # poort weg, bezet, enz.
            tekst, reden = "", "seriele fout: %s" % e
        duur = time.time() - t0

        logpad = os.path.join(logmap, "run%02d_%s.txt"
                              % (run, datetime.now().strftime("%H%M%S")))
        with open(logpad, "w", encoding="utf-8") as lf:
            lf.write(tekst)
        sha = sha256_bestand(logpad)
        logbestand = os.path.basename(logpad)

        # bewijs dat er echt geherstart is
        if not reden and not any(m in tekst for m in BOOT_MERKERS):
            reden = "geen ROM-bootmerker: reset niet aantoonbaar"

        basis = {"datum_iso": t_start, "serie": a.serie, "bordtype": a.bordtype,
                 "bordnaam": a.bordnaam, "poort": a.poort, "soort": a.soort,
                 "run": run, "logbestand": logbestand, "log_sha256": sha}

        rijen, chk = [], []
        if not reden:
            if a.soort == "04":
                rijen, reden = parse_04(tekst)
            else:
                rijen = parse_07_fasen(tekst)
                chk = parse_07_checkpoints(tekst)
                reden = keur_07(rijen, chk) if rijen else "geen FASE07-regels geparst"

        geldig = "ja" if not reden else "NEE"
        if geldig == "ja":
            n_geldig += 1

        if rijen:
            for r in rijen:
                rij = dict(basis)
                rij.update(r)
                rij["geldig"] = geldig
                rij["reden"] = reden
                wh.writerow({k: rij.get(k, "") for k in velden})
        else:
            rij = dict(basis)
            rij.update({"geldig": "NEE", "reden": reden or "leeg", "pass_of_fase": "ONGELDIG"})
            wh.writerow({k: rij.get(k, "") for k in velden})
        fh.flush()

        if a.soort == "07" and chk:
            for c in chk:
                rij = {k: basis.get(k, "") for k in V_07C}
                rij.update({k: v for k, v in c.items() if k in V_07C})
                rij["run"] = run
                rij["geldig"] = geldig
                rij["reden"] = reden
                wc.writerow(rij)
            fc.flush()

        manifest["runs"].append({
            "run": run, "start": t_start, "duur_s": round(duur, 1),
            "geldig": geldig, "reden": reden, "logbestand": logbestand,
            "sha256": sha, "bytes": os.path.getsize(logpad),
            "bootcontext": lees_bootcontext(tekst),
        })

        if geldig == "ja":
            for r in rijen:
                print("  %-20s %s it/s%s"
                      % (r["pass_of_fase"], r.get("score_it_per_s", "?"),
                         ("  zelftest " + r["zelftest"]) if r.get("zelftest", "nvt") != "nvt" else ""),
                      flush=True)
        else:
            print("  !! ONGELDIG: %s  (log: %s)" % (reden, logbestand), flush=True)

    fh.close()
    if fc:
        fc.close()

    manifest["eind_utc"] = datetime.now(timezone.utc).isoformat(timespec="seconds")
    manifest["runs_geldig"] = n_geldig
    with open(os.path.join(bordmap, "manifest_%s.json" % a.soort), "w",
              encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)

    print("=== %s %s klaar: %d/%d geldig -> %s ==="
          % (a.bordnaam, a.soort, n_geldig, a.runs, bordmap), flush=True)
    # 0 = alle runs geldig, 2 = gedeeltelijk, 1 = niets bruikbaar.
    # Gedeeltelijk mag niet als "geslaagd" in de eindsamenvatting belanden.
    if n_geldig == a.runs:
        return 0
    return 2 if n_geldig > 0 else 1


if __name__ == "__main__":
    sys.exit(main())
