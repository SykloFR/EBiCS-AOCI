"""Rampe douce de l'outil vecteur fixe par CAN, angle fixe, pour vérifier signe et échelle du courant sur un contrôleur
sans SWD (vélo). u_d monte par paliers ; à chaque palier : moyennes de i_d, i_q (E000), courant efficace et crête de phase
(E100), Ibat (E101). Attendu (validé au banc le 26/10, contrôleur n° 1) : i_d > 0 et croissant avec u_d, i_q ≈ 0.
Arrêt au premier seuil : |i_d| ou |i_q| moyen >= SEUIL pts, défaut, MOE retombé, Ibat >= 0,4 A. OL_STOP à la fin.
Correctifs F1 / F2 : affiche la variante et les offsets des deux méthodes (E205, E207), puis un premier palier à u_d 0
pendant 2 s (aucune tension appliquée : |i_d| et |i_q| moyens doivent rester < SEUIL_UD0 = 5 pts, sinon arrêt).
Usage : python tools/bench/t20_rampe_vecteur_can.py SORTIE_PREFIXE [ud_max=30] [pas=2] [seuil=8] [angle_deg=0]"""
import csv, os, statistics, sys, time
sys.path.insert(0, os.path.dirname(__file__))
from bench_can_cmd import Banc, STATUTS

PREFIXE = sys.argv[1]
UD_MAX = int(sys.argv[2]) if len(sys.argv) > 2 else 30
PAS = int(sys.argv[3]) if len(sys.argv) > 3 else 2
SEUIL = float(sys.argv[4]) if len(sys.argv) > 4 else 8
ANGLE = float(sys.argv[5]) if len(sys.argv) > 5 else 0.0
SEUIL_UD0 = 5
paliers, arret = [], None
ATTENDRE = float(os.environ.get("T20_ATTENDRE", "0"))   # s : attendre le démarrage du contrôleur (hello E203)
with Banc() as b:
    time.sleep(1.2)
    t_att = time.time()
    while ATTENDRE and time.time() - t_att < ATTENDRE and not b.dernier.get(0xE205, {}).get("calibration_faite"):
        time.sleep(0.05)
    if ATTENDRE:
        time.sleep(1.2)                                    # E200 / E205 à jour
        print(f"contrôleur vu après {time.time() - t_att:.1f} s d'attente", flush=True)
    e200, e205 = b.dernier.get(0xE200, {}), b.dernier.get(0xE205, {})
    print(f"départ : défaut {e200.get('defaut')}, calibration {e205.get('calibration_faite')}, Hall {b.dernier.get(0xE001, {}).get('hall_gpio')}", flush=True)
    e207, e204 = b.dernier.get(0xE207, {}), b.dernier.get(0xE204, {})
    print(f"variante {e207.get('variante')}, seuils {e204.get('ph_max')} / {e204.get('ph_trip')} pts ; offsets bras bas "
          f"{[e205.get(k) for k in ('off_ph1', 'off_ph2', 'off_ph3')]}, pont à 50 % "
          f"{[e207.get(k) for k in ('pont_ph1', 'pont_ph2', 'pont_ph3')]}, pont appliqués {e205.get('pont_appliques')}", flush=True)
    if e200.get("defaut") or not e205.get("calibration_faite"):
        sys.exit("ARRET : défaut ou calibration non faite (remise sous tension)")
    b.demarrer_heartbeat(); time.sleep(0.8)
    try:
        ud, premier = 0, True
        while (premier or ud < UD_MAX) and arret is None:
            if not premier:
                ud = min(ud + PAS, UD_MAX)
            st = b.ol_set(ANGLE, ud)
            if st != 0:
                arret = f"OL_SET u_d {ud} : {STATUTS.get(st, st)}"; break
            ech = []
            t0 = time.time()
            while time.time() - t0 < (2.0 if premier else 0.5):
                time.sleep(0.02)
                e0, e1, e100, e101, e2 = (b.dernier.get(k, {}) for k in (0xE000, 0xE001, 0xE100, 0xE101, 0xE200))
                ech.append((e0.get("id"), e0.get("iq"), e100.get("ph1_eff"), e100.get("crete"), e101.get("ibat"), e1.get("hall_gpio")))
                if e2.get("defaut") or not e0.get("moe"):
                    arret = f"u_d {ud} : défaut {e2.get('defaut')}, MOE {e0.get('moe')}"; break
            v = [x for x in ech if x[0] is not None]
            m = dict(ud=ud, id=round(statistics.mean(x[0] for x in v), 2), iq=round(statistics.mean(x[1] for x in v), 2),
                     ph1_eff=max(x[2] or 0 for x in v), crete=max(x[3] or 0 for x in v), ibat=max(x[4] or 0 for x in v),
                     hall=sorted({x[5] for x in v}))
            paliers.append(m)
            print(f"u_d {ud:3d} : i_d {m['id']:+6.2f}  i_q {m['iq']:+6.2f}  ph1 eff {m['ph1_eff']}  crête {m['crete']}  Ibat {m['ibat']}  Hall {m['hall']}", flush=True)
            if arret is None and max(abs(m["id"]), abs(m["iq"])) >= (SEUIL_UD0 if premier else SEUIL):
                arret = f"seuil {SEUIL_UD0 if premier else SEUIL} pts atteint à u_d {ud}"
            premier = False
            if arret is None and m["ibat"] >= 0.4:
                arret = f"Ibat {m['ibat']} A à u_d {ud}"
    finally:
        print("OL_STOP :", STATUTS.get(b.ol_stop(), "pas de réponse"), flush=True)
        time.sleep(0.2)
        print(f"fin : MOE {b.dernier.get(0xE000, {}).get('moe')}, défaut {b.dernier.get(0xE200, {}).get('defaut')}", flush=True)
print("arrêt :", arret or f"rampe complète jusqu'à u_d {UD_MAX}")
with open(PREFIXE + ".csv", "w", newline="", encoding="utf-8") as f:
    if paliers:
        w = csv.DictWriter(f, fieldnames=list(paliers[0])); w.writeheader(); w.writerows(paliers)
