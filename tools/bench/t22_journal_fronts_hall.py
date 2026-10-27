"""Journal des fronts Hall pendant un walk (porte 4, 26/10/2026 : walk 5 pts sans chaine, le moteur vibre sans tourner).
Lance WALK_START(ILIM) jusqu'au defaut, a l'arret du pont par EBiCS ou a DUREE s, WALK_STOP, puis relit par CAN (E20A) les
HJ_N derniers fronts Hall du passage : cas EBiCS xy, duree depuis le front precedent, 6-step, sens detecte, angle de
secteur retenu. Bilan : fronts avant / arriere (sequence avant EBiCS 6-4-5-1-3-2 = theta croissant au releve A5),
avance nette en secteurs, allers-retours sur une meme frontiere (rotor qui oscille), fronts hors sequence.
Si MOE retombe, le verdict attend une E200 recue APRES le front MOE (0,5 s au plus) : defaut si elle en porte un, arret
EBiCS sans defaut seulement si elle dit 0, indetermine sinon. Le CSV porte l'heure de reception de chaque front (t_rx_s,
depuis WALK_START).
Capture avant trip (E20B, build e7ed5b5 + capture) : les PT_N dernieres periodes PWM vues par le trip, relues apres l'arret
et ecrites dans PREFIXE_avant_trip.csv (entrees du trip ph1..ph3 en pts, resultat de F4, 6-step) ; absente des builds
anterieurs (rien n'est ecrit).
Conditions : moteur monte, chaine retiree, personne sur le velo, alim limitee, arret physique a portee.
Usage : python tools/bench/t22_journal_fronts_hall.py PREFIXE [ilim_pts=5] [duree_s=3]"""
import csv, os, sys, time
sys.path.insert(0, os.path.dirname(__file__))
from bench_can_cmd import Banc, STATUTS

PREFIXE = sys.argv[1]
ILIM = int(sys.argv[2]) if len(sys.argv) > 2 else 5
DUREE = float(sys.argv[3]) if len(sys.argv) > 3 else 3.0
if not 1 <= ILIM <= 5:
    sys.exit("ILIM entre 1 et 5 pts (GO Fangyan : 3 et 5 points)")
AVANT = {64, 45, 51, 13, 32, 26}
ARRIERE = {46, 62, 23, 31, 15, 54}

with Banc() as b:
    time.sleep(1.5)
    e200, e205 = b.frais(0xE200) or {}, b.frais(0xE205) or {}
    print(f"depart : defaut {e200.get('defaut')}, calibration {e205.get('calibration_faite')}, "
          f"journal {b.dernier.get(0xE20A)}", flush=True)
    if e200.get("defaut") is None or e200.get("defaut") or not e205.get("calibration_faite") or 0xE20A not in b.dernier:
        sys.exit("ARRET : defaut, calibration non faite, E200 / E205 pas fraiches ou build sans journal (remise sous tension / bon build ?)")
    seq0 = b.dernier[0xE20A].get("hj_seq")
    b.demarrer_heartbeat(); time.sleep(0.8)
    arret, t0, t0_mono = None, time.time(), time.monotonic()
    try:
        st = b.walk_start(ILIM)
        print(f"WALK_START {ILIM} pts :", STATUTS.get(st, "pas de reponse"), flush=True)
        if st != 0:
            arret = "walk refuse"
        time.sleep(0.2)
        while arret is None and time.time() - t0 < DUREE:
            time.sleep(0.02)
            e0, e2 = b.dernier.get(0xE000, {}), b.dernier.get(0xE200, {})
            if e2.get("defaut"):
                arret = f"defaut {e2['defaut']}"
            elif b.etat(0xE000) in ("fige", "absent"):        # plus de 5 periodes (100 ms) sans E000
                arret = f"telemetrie E000 figee ({b.etat(0xE000)})"
            elif not e0.get("moe"):
                # la E000 (MOE = 0) arrive avant la E200 qui porte le defaut : attendre une E200 recue apres le front
                d = b.defaut_apres(b.t_moe0 or time.monotonic() - 0.05)
                arret = (f"defaut {d} (MOE retombe, E200 recue apres)" if d else
                         "MOE retombe, arret EBiCS sans defaut (E200 recue apres : defaut 0)" if d == 0 else
                         "MOE retombe, defaut indetermine (aucune E200 recue dans les 0,5 s apres)")
            elif (b.dernier.get(0xE101, {}).get("ibat") or 0) >= 1.5:
                arret = "Ibat >= 1,5 A"
    finally:
        print("WALK_STOP :", STATUTS.get(b.walk_stop(), "pas de reponse"), flush=True)
        t_stop = time.monotonic()
    print(f"arret : {arret or f'duree {DUREE} s'} apres {time.time() - t0:.2f} s", flush=True)
    time.sleep(4.0)                       # plus de deux tours de E20A (64 rangs a 50 Hz), un tour et demi de E20B (128 rangs)
    seq = b.dernier.get(0xE20A, {}).get("hj_seq")
    pt_seq = b.dernier.get(0xE20B, {}).get("pt_seq")
    avant_trip = sorted((v for (s, r), v in b.avant_trip.items() if s == pt_seq), key=lambda v: v["rang"])
    fronts = sorted((v for (s, r), v in b.fronts.items() if s == seq), key=lambda v: v["rang"])
    defaut = b.defaut_apres(t_stop)                       # E200 recue apres WALK_STOP ; None : indetermine
    defaut = "indetermine (aucune E200 apres WALK_STOP)" if defaut is None else defaut

if seq == seq0:
    sys.exit("ARRET : journal non relance (le pont ne s'est pas active ?)")
with open(PREFIXE + ".csv", "w", newline="", encoding="utf-8") as f:
    w = csv.writer(f)
    w.writerow(["t_rx_s", "rang", "cas", "transition", "sens_sequence", "duree_ms", "six_step", "sens_detecte", "angle_deg"])
    for v in fronts:
        c = v["cas"]
        sq = "avant" if c in AVANT else ("arriere" if c in ARRIERE else "hors sequence")
        w.writerow([round(v["t_rx"] - t0_mono, 3), v["rang"], c, f"{c // 10}->{c % 10}", sq, round(v["tics"] * 0.002, 3), v["six_step"], v["sens"],
                    v["angle_deg"]])
print(f"journal n {seq} : {len(fronts)} fronts, defaut final {defaut}")
print(" rang  trans  sequence      ms   6step sens angle")
for v in fronts:
    c = v["cas"]
    sq = "avant" if c in AVANT else ("arriere" if c in ARRIERE else "HORS")
    print(f"{v['rang']:5d}  {c // 10}->{c % 10}  {sq:8s} {v['tics'] * 0.002:8.3f} {v['six_step']:5d} {v['sens']:+4d} {v['angle_deg']:6.1f}")
cas = [v["cas"] for v in fronts]
av, ar = sum(c in AVANT for c in cas), sum(c in ARRIERE for c in cas)
hors = len(cas) - av - ar
allers_retours = sum(1 for a, c in zip(cas, cas[1:]) if a % 10 == c // 10 and a // 10 == c % 10)
print(f"bilan : {av} avant, {ar} arriere, {hors} hors sequence ; avance nette {av - ar} secteurs "
      f"({(av - ar) / 6:+.1f} tour electrique) ; allers-retours sur une meme frontiere : {allers_retours}")
etats = sorted({c % 10 for c in cas} | {c // 10 for c in cas})
print(f"etats vus : {etats} ; duree mediane entre fronts "
      f"{sorted(v['tics'] for v in fronts)[len(fronts) // 2] * 0.002 if fronts else 0:.3f} ms")

F4 = {0: "appliquee", 1: "refus masque", 2: "refus saturation", 3: "refus mode commun"}
if avant_trip:
    with open(PREFIXE + "_avant_trip.csv", "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["rang", "ph1", "ph2", "ph3", "f4", "six_step"])
        for v in avant_trip:
            w.writerow([v["rang"], *v["ph"], "non evaluee" if v["f4_non_evaluee"] else F4[v["f4"]], v["six_step"]])
    n = len(avant_trip)
    crete = max(max(abs(x) for x in v["ph"]) for v in avant_trip)
    refus = {F4[k]: sum(1 for v in avant_trip if not v["f4_non_evaluee"] and v["f4"] == k) for k in (1, 2, 3)}
    print(f"capture avant trip n {pt_seq} : {n} periodes (rangs 0..{avant_trip[-1]['rang']}, manquants "
          f"{avant_trip[-1]['rang'] + 1 - n}), crete |ph| {crete} pts, refus F4 {refus}")
    print(" rang   ph1  ph2  ph3  F4")
    for v in avant_trip[-12:]:
        print(f"{v['rang']:5d} {v['ph'][0]:5d} {v['ph'][1]:4d} {v['ph'][2]:4d}  "
              f"{'non evaluee' if v['f4_non_evaluee'] else F4[v['f4']]}")
