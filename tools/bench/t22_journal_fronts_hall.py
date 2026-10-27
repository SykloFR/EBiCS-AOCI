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
Serie temporelle (27/10/2026) : E000 / E001 / E101 / E102 (i_q, consigne, i_d, u_d, u_q, u_abs, Ibat, erps, 20 a 50 ms)
gardees du WALK_START a l'arret, ecrites dans PREFIXE_telemetrie.csv ; bilan des 200 ms avant la chute de MOE : u_abs
contre le plafond _U_MAX (E204), suivi de la consigne i_q.
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
    u_max = (b.dernier.get(0xE204) or {}).get("u_max")
    b.flux = []
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
    flux, b.flux = b.flux, None
    time.sleep(4.0)                       # plus de deux tours de E20A (64 rangs a 50 Hz), un tour et demi de E20B (128 rangs)
    seq = b.dernier.get(0xE20A, {}).get("hj_seq")
    pt_seq = b.dernier.get(0xE20B, {}).get("pt_seq")
    avant_trip = sorted((v for (s, r), v in b.avant_trip.items() if s == pt_seq), key=lambda v: v["rang"])
    enrichie = {r: v for (s, r), v in b.avant_trip_u.items() if s == pt_seq}   # E20C, absente avant la capture enrichie
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
        w.writerow(["rang", "ph1", "ph2", "ph3", "f4", "six_step", "ud", "uq", "angle_deg", "hall", "cercle", "foc",
                    "tenu"])
        for v in avant_trip:
            u = enrichie.get(v["rang"], {})
            w.writerow([v["rang"], *v["ph"], "non evaluee" if v["f4_non_evaluee"] else F4[v["f4"]], v["six_step"]]
                       + [u.get(k, "") for k in ("ud", "uq", "angle_deg", "hall", "cercle", "foc", "tenu")])
    n = len(avant_trip)
    crete = max(max(abs(x) for x in v["ph"]) for v in avant_trip)
    refus = {F4[k]: sum(1 for v in avant_trip if not v["f4_non_evaluee"] and v["f4"] == k) for k in (1, 2, 3)}
    print(f"capture avant trip n {pt_seq} : {n} periodes (rangs 0..{avant_trip[-1]['rang']}, manquants "
          f"{avant_trip[-1]['rang'] + 1 - n}), crete |ph| {crete} pts, refus F4 {refus}")
    print(" rang   ph1  ph2  ph3  F4")
    for v in avant_trip[-12:]:
        print(f"{v['rang']:5d} {v['ph'][0]:5d} {v['ph'][1]:4d} {v['ph'][2]:4d}  "
              f"{'non evaluee' if v['f4_non_evaluee'] else F4[v['f4']]}")

# serie temporelle : une ligne par trame, colonnes vides hors de la trame
COLS = ["iq_cons", "iq", "id", "moe", "ud", "uq", "uabs", "erps", "ibat", "vbat"]
with open(PREFIXE + "_telemetrie.csv", "w", newline="", encoding="utf-8") as f:
    w = csv.writer(f)
    w.writerow(["t_s", "trame"] + COLS)
    for t, cle, v in flux:
        w.writerow([round(t - t0_mono, 4), f"E{cle & 0xFFF:03X}"] + [v.get(c, "") for c in COLS])
t_chute = next((t for t, cle, v in flux if cle == 0xE000 and not v.get("moe") and t - t0_mono > 0.25), None)
fin = t_chute if t_chute is not None else (flux[-1][0] if flux else t0_mono)
fen = [(t, cle, v) for t, cle, v in flux if fin - 0.2 <= t < fin]
e1 = [v for t, cle, v in fen if cle == 0xE001]
e0 = [v for t, cle, v in fen if cle == 0xE000 and v.get("moe")]
if e1 and e0 and u_max:
    sat = sum(1 for v in e1 if v["uabs"] >= 0.97 * u_max) / len(e1)
    ecart = [abs(v["iq"] - v["iq_cons"]) for v in e0]
    print(f"200 ms avant {'la chute de MOE' if t_chute else 'la fin'} : u_abs {min(v['uabs'] for v in e1)}..{max(v['uabs'] for v in e1)}"
          f" (plafond {u_max}, {sat:.0%} des E001 >= 97 %), u_d {[v['ud'] for v in e1[-4:]]}, u_q {[v['uq'] for v in e1[-4:]]}, "
          f"i_q {[v['iq'] for v in e0[-4:]]} pour {e0[-1]['iq_cons']}, i_d {[v['id'] for v in e0[-4:]]}, "
          f"ecart i_q max {max(ecart)}")
    print("lecture (criteres ecrits avant) : " + (
        "PLAFOND DE TENSION ATTEINT (H1 : regulateur sature avant le trip)" if sat >= 0.5 else
        "tension sous le plafond et i_q suivi (H2 : evenement brutal, angle ?)" if max(ecart) <= 2 else
        "tension sous le plafond mais i_q non suivi : ni H1 ni H2 nettement, voir la serie"))
else:
    print(f"serie temporelle : {len(flux)} trames, fenetre avant l'arret incomplete (E204 {u_max}) : pas de bilan")

# capture enrichie (capture-enrichie-attendu-20261027.md) : sauts d'angle aux fronts Hall, oscillation de u_d / u_q
if avant_trip and enrichie:
    import math
    lig = [(v, enrichie.get(v["rang"])) for v in avant_trip]
    lig = [(v, u) for v, u in lig if u and u["foc"]]
    pas = [((u2["angle_deg"] - u1["angle_deg"] + 180) % 360 - 180, u1["hall"], u2["hall"]) for (_, u1), (_, u2) in zip(lig, lig[1:])]
    courant = sorted(p for p, h1, h2 in pas if h1 == h2)
    med = courant[len(courant) // 2] if courant else 0.0
    sauts = [(h1, h2, round(p - med, 1)) for p, h1, h2 in pas if h1 != h2]
    u_abs = [math.hypot(u["ud"], u["uq"]) for _, u in lig]
    ref = max(1.0, sum(u_abs) / len(u_abs)) if u_abs else 1.0
    cc = lambda k: (max(u[k] for _, u in lig) - min(u[k] for _, u in lig)) / ref if lig else 0.0
    cercle = sum(u["cercle"] for _, u in lig) / len(lig) if lig else 0.0
    print(f"capture enrichie : {len(lig)} periodes FOC, pas d'angle median {med:+.1f} deg/periode, "
          f"cercle {cercle:.0%}, u_d crete a crete {cc('ud'):.0%} de u_abs, u_q {cc('uq'):.0%}")
    print("sauts d'angle aux fronts Hall (front : saut - pas median, deg) :", sauts)
    n_tenu = sum(1 for _, u in lig if u.get("tenu"))
    pepins = [s for s in sauts if s[2] >= 20]
    print(f"correctif des pepins (correctif-pepins-angle-attendu-20261027.md) : {n_tenu} periodes tenues, "
          f"{len(pepins)} saut(s) >= +20 deg aux fronts -> {'CONFORME' if not pepins else 'NON CONFORME'}")
    grands = [s for s in sauts if abs(s[2]) >= 8]
    meme_signe = grands and (all(s[2] > 0 for s in grands) or all(s[2] < 0 for s in grands))
    print("lecture (criteres ecrits avant) : " + (
        "H1 : limitation en cercle sur plus de 50 % des periodes" if cercle > 0.5 else
        "H3 : sauts d'angle >= 8 deg aux fronts" + (" de meme signe" if meme_signe else " (signes mixtes)")
        if sauts and len(grands) >= max(1, len(sauts) // 2) else
        "H4 : u_d / u_q oscillent (>= 20 % de u_abs)" if max(cc("ud"), cc("uq")) >= 0.20 else
        "H3 refutee (sauts <= 3 deg)" if sauts and all(abs(s[2]) <= 3 for s in sauts) else
        "aucune hypothese nette : arret et revue"))
