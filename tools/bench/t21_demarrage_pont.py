"""Diagnostic du demarrage du pont et precharge F3 (porte 4, 26/10/2026 : defaut 3 des le palier u_d 0 sur le velo).
Regle la precharge (OP 0x30), active l'outil vecteur fixe a u_d donne pendant DUREE s (ou jusqu'au defaut), OL_STOP, puis
relit par CAN la capture des CAP_N premieres periodes PWM apres MOE = 1 (E208, E209) : courants ph1, ph2, ph3 apres
offset, avant reconstruction, satures a +-127 pts. Ecrit PREFIXE.csv et affiche le tableau (* : |i| >= 16 pts, seuil du
trip ; les ~16,5 x precharge premieres periodes sont la precharge, trois bas fermes).
Un defaut verrouille exige une remise sous tension avant l'essai suivant.
Usage : python tools/bench/t21_demarrage_pont.py PREFIXE [precharge_ms=0] [ud=0] [duree_s=1.0] [angle_deg=0]"""
import csv, os, sys, time
sys.path.insert(0, os.path.dirname(__file__))
from bench_can_cmd import Banc, STATUTS

PREFIXE = sys.argv[1]
PRE = int(sys.argv[2]) if len(sys.argv) > 2 else 0
UD = int(sys.argv[3]) if len(sys.argv) > 3 else 0
DUREE = float(sys.argv[4]) if len(sys.argv) > 4 else 1.0
ANGLE = float(sys.argv[5]) if len(sys.argv) > 5 else 0.0
SEUIL = 16

with Banc() as b:
    time.sleep(1.5)
    e200, e205, e209 = (b.dernier.get(k, {}) for k in (0xE200, 0xE205, 0xE209))
    print(f"depart : defaut {e200.get('defaut')}, calibration {e205.get('calibration_faite')}, capture {e209}", flush=True)
    if e200.get("defaut") or not e205.get("calibration_faite") or not e209:
        sys.exit("ARRET : defaut, calibration non faite ou build sans capture (remise sous tension / bon build ?)")
    st = b.precharge(PRE)
    print(f"precharge {PRE} ms :", STATUTS.get(st, "pas de reponse"), flush=True)
    if st != 0:
        sys.exit("ARRET : precharge refusee")
    seq0 = e209.get("cap_seq")
    b.demarrer_heartbeat(); time.sleep(0.8)
    defaut = None
    try:
        st = b.ol_set(ANGLE, UD)
        print(f"OL_SET u_d {UD} :", STATUTS.get(st, "pas de reponse"), flush=True)
        t0 = time.time()
        while st == 0 and time.time() - t0 < DUREE:
            time.sleep(0.02)
            if b.dernier.get(0xE200, {}).get("defaut"):
                defaut = b.dernier[0xE200]["defaut"]
                break
    finally:
        print("OL_STOP :", STATUTS.get(b.ol_stop(), "pas de reponse"), flush=True)
    t0 = time.time()
    while time.time() - t0 < 4:
        e209 = b.dernier.get(0xE209, {})
        if e209.get("cap_seq") != seq0 and e209.get("cap_n") == e209.get("cap_N"):
            break
        time.sleep(0.05)
    else:
        sys.exit(f"ARRET : capture non terminee ou non relancee ({e209})")
    sq, n = e209["cap_seq"], e209["cap_N"]
    b.capture.clear()
    time.sleep(3.0)                                       # plus de deux tours complets de E208
    cap = {i: v for i, (s, v) in b.capture.items() if s == sq}
    defaut = b.dernier.get(0xE200, {}).get("defaut", defaut)

lignes = [(i,) + cap[i] for i in range(n) if i in cap]
with open(PREFIXE + ".csv", "w", newline="", encoding="utf-8") as f:
    w = csv.writer(f)
    w.writerow(["periode", "ph1", "ph2", "ph3"])
    w.writerows(lignes)
pre_p = round(PRE * 16.5)
print(f"capture n {sq} : {len(lignes)}/{n} periodes recues, precharge {PRE} ms (~{pre_p} periodes), defaut vu a la "
      f"periode {e209['cap_trip'] if e209['cap_trip'] != 255 else 'aucune'}, CHCTL2 {e209['chctl2']}, defaut final {defaut}")
for i, a, c, d in lignes:
    marque = "*" if max(abs(a), abs(c), abs(d)) >= SEUIL else " "
    zone = "pre" if i < pre_p else "   "
    print(f"{i:4d} {zone} {a:5d} {c:5d} {d:5d} {marque}")
premier = next((i for i, a, c, d in lignes if max(abs(a), abs(c), abs(d)) >= SEUIL), None)
print(f"premiere periode >= {SEUIL} pts : {premier}")
