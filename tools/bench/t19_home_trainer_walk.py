"""Home trainer (avis Fangyan 26/10, GO 3 et 5 points) : walk assist lancé par CAN, consigne plafonnée, durée courte.
Moteur monté, roue arrière sur home trainer, personne sur le vélo, alim limitée, ARRÊT PHYSIQUE À PORTÉE (bouton de l'alim
ou coupe-circuit : le PC n'est pas un arrêt d'urgence). Build de banc avec inc/bench_hall_angles.h (angles relevés par t18).

Pré-contrôles par la télémétrie : défaut 0, calibration faite, état Hall valide, roue arrêtée. Puis WALK_START(ilim),
journal de toutes les trames pendant DUREE s, WALK_STOP, MOE vérifié à 0. Arrêt immédiat (WALK_STOP, puis ESTOP si MOE
reste à 1) si : défaut, courant batterie >= IBAT_MAX, crête de phase >= 15 pts sur 3 trames E100 de suite (trip à 16),
T contrôleur >= 60 °C, vitesse > 8 km/h, i_q > ilim + 3 pts sur 1 s.
Usage : python tools/bench/t19_home_trainer_walk.py SORTIE_PREFIXE ILIM_PTS [duree_s=10] [ibat_max_A=1.5]"""
import csv, json, os, sys, time
sys.path.insert(0, os.path.dirname(__file__))
from bench_can_cmd import Banc, STATUTS

PREFIXE, ILIM = sys.argv[1], int(sys.argv[2])
DUREE = float(sys.argv[3]) if len(sys.argv) > 3 else 10.0
IBAT_MAX = float(sys.argv[4]) if len(sys.argv) > 4 else 1.5
if not 1 <= ILIM <= 5:
    sys.exit("ILIM entre 1 et 5 pts (GO Fangyan : 3 et 5 points ; 8 points conditionnel)")

journal, arret, bilan = [], None, {}
with Banc() as b:
    time.sleep(1.2)
    e200, e205 = b.dernier.get(0xE200, {}), b.dernier.get(0xE205, {})
    pre = dict(defaut=e200.get("defaut"), calibration=e205.get("calibration_faite"), hall_valide=e205.get("hall_valide"),
               roue_arretee=e205.get("roue_arretee"), hello=b.dernier.get(0xE203))
    print("pré-contrôles :", pre, flush=True)
    if pre["defaut"] or not pre["calibration"] or not pre["hall_valide"] or not pre["roue_arretee"]:
        sys.exit("ARRET : défaut, calibration non faite, codeur muet ou roue en mouvement")
    b.demarrer_heartbeat(); time.sleep(0.8)
    st = b.walk_start(ILIM)
    print(f"WALK_START {ILIM} pts : {STATUTS.get(st, st)}", flush=True)
    if st != 0:
        sys.exit("ARRET : WALK_START refusé")
    t0, crete_n, depasse_t = time.time(), 0, None
    try:
        while time.time() - t0 < DUREE:
            time.sleep(0.05)
            e0, e100, e101 = b.dernier.get(0xE000, {}), b.dernier.get(0xE100, {}), b.dernier.get(0xE101, {})
            e102, e200, e201 = b.dernier.get(0xE102, {}), b.dernier.get(0xE200, {}), b.dernier.get(0xE201, {})
            ligne = dict(t=round(time.time() - t0, 3), moe=e0.get("moe"), iq_cons=e0.get("iq_cons"), iq=e0.get("iq"),
                         id=e0.get("id"), crete=e100.get("crete"), ph1=e100.get("ph1_eff"), vbat=e101.get("vbat"),
                         ibat=e101.get("ibat"), vitesse=e102.get("vitesse"), erps=e102.get("erps"),
                         defaut=e200.get("defaut"), t_ctrl=e201.get("t_ctrl"))
            journal.append(ligne)
            crete_n = crete_n + 1 if (ligne["crete"] or 0) >= 15 else 0
            depasse_t = (depasse_t or time.time()) if (ligne["iq"] or 0) > ILIM + 3 else None
            if ligne["defaut"]:
                arret = f"défaut {ligne['defaut']}"
            elif (ligne["ibat"] or 0) >= IBAT_MAX:
                arret = f"courant batterie {ligne['ibat']} A"
            elif crete_n >= 3:
                arret = f"crête de phase {ligne['crete']} pts sur 3 trames"
            elif (ligne["t_ctrl"] or 0) >= 60:
                arret = f"T contrôleur {ligne['t_ctrl']} °C"
            elif (ligne["vitesse"] or 0) > 8:
                arret = f"vitesse {ligne['vitesse']} km/h"
            elif depasse_t and time.time() - depasse_t > 1.0:
                arret = f"i_q {ligne['iq']} > ilim + 3 pendant 1 s"
            if arret:
                break
    finally:
        st = b.walk_stop()
        time.sleep(0.3)
        moe = b.dernier.get(0xE000, {}).get("moe")
        if moe:
            b.estop(); time.sleep(0.2)
        bilan = dict(walk_stop=STATUTS.get(st, st), moe_apres=moe, estop_envoye=bool(moe),
                     defaut_final=b.dernier.get(0xE200, {}).get("defaut"))
        print("fin :", bilan, flush=True)

with open(PREFIXE + ".csv", "w", newline="", encoding="utf-8") as f:
    if journal:
        w = csv.DictWriter(f, fieldnames=list(journal[0])); w.writeheader(); w.writerows(journal)
vals = lambda k: [l[k] for l in journal if l[k] is not None]
resume = dict(ilim=ILIM, duree_s=DUREE, arret=arret, precontroles=pre, bilan=bilan,
              iq_moy=round(sum(vals("iq")) / max(1, len(vals("iq"))), 2), iq_max=max(vals("iq"), default=None),
              ibat_max=max(vals("ibat"), default=None), crete_max=max(vals("crete"), default=None),
              vitesse_max=max(vals("vitesse"), default=None), t_ctrl_max=max(vals("t_ctrl"), default=None))
with open(PREFIXE + ".json", "w", encoding="utf-8") as f:
    json.dump(resume, f, ensure_ascii=False, indent=1)
print(json.dumps(resume, ensure_ascii=False, indent=1))
