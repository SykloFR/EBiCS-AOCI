"""Sens physique du codeur, pont coupe (porte 4, 27/10/2026). ECOUTE SEULE : aucune trame n'est emise, MOE reste a 0.
Question : le sens theta croissant d'EBiCS (E 6-4-5-1-3-2, releve A5) est-il l'avant ou l'arriere physique du moteur ?
Le firmware d'origine entraine le moteur en O 4-6-2-3-1-5 = E 1-5-4-6-2-3 (theta decroissant). Si c'est son avant,
le walk d'EBiCS (couple vers theta croissant) fait tourner le moteur a l'envers derriere sa roue libre : plateau
immobile, bruit de cliquet (« vibre »), fronts Hall reguliers.

Geste (chaine retiree, alim limitee, apres la calibration, personne ne pedale) : tourner le PLATEAU a la main EN
ARRIERE, lentement (quelques centimetres en 10-20 s), apres avoir rattrape le jeu. En arriere, la roue libre de sortie
du moteur se bloque et entraine le rotor en arriere ; en avant, elle patine et rien ne bouge.

Resultat attendu ecrit avant l'essai (hypothese « theta croissant = arriere physique ») : en tournant en arriere,
transitions E 6->4->5->1->3->2 majoritaires (theta croissant), >= 12 transitions adjacentes, < 20 % sautees.
  - theta croissant majoritaire  -> hypothese confirmee : le walk pousse le moteur en arriere ;
  - theta decroissant majoritaire -> hypothese refutee : chercher ailleurs (journal E20A) ;
  - moins de 12 transitions       -> non concluant (rotor non entraine en arriere ou trop peu tourne) ;
  - plus de 20 % de sauts          -> trop vite pour E001 a 50 Hz : recommencer plus lentement.
Usage : python tools/bench/t23_sens_codeur_plateau.py PREFIXE [duree_s=20]"""
import csv, json, os, sys, time
import can
sys.path.insert(0, os.path.dirname(__file__))
from bench_can_decode import decode  # noqa: E402

PREFIXE = sys.argv[1]
DUREE = float(sys.argv[2]) if len(sys.argv) > 2 else 20.0
SEQ = [6, 4, 5, 1, 3, 2]                       # theta croissant (A5) ; suivant d'un etat = +1 secteur
POS = {e: i for i, e in enumerate(SEQ)}
N_MIN, PART_SAUTS_MAX = 12, 0.20

bus = can.Bus(interface="pcan", channel="PCAN_USBBUS1", bitrate=250000)   # aucune emission dans ce script
lignes, moe_vu, defaut = [], False, None
t0 = time.monotonic()
print(f"ecoute {DUREE:.0f} s : tourner le plateau EN ARRIERE, lentement, maintenant", flush=True)
try:
    while time.monotonic() - t0 < DUREE:
        m = bus.recv(0.05)
        if m is None or not m.is_extended_id or m.arbitration_id >> 16 != 0x1EE8:
            continue
        cle, t = m.arbitration_id & 0xFFFF, time.monotonic() - t0
        d = decode(cle, bytes(m.data))
        if cle == 0xE000 and d.get("moe"):
            moe_vu = True
        elif cle == 0xE200:
            defaut = d.get("defaut")
        elif cle == 0xE001:
            lignes.append((round(t, 3), d["hall_gpio"], d["hall_etat"]))
finally:
    bus.shutdown()

with open(PREFIXE + ".csv", "w", newline="", encoding="utf-8") as f:
    w = csv.writer(f)
    w.writerow(["t_rx_s", "hall_gpio", "hall_etat_ebics"])
    w.writerows(lignes)

etats = [h for _, h, _ in lignes]
transitions = [(a, b) for a, b in zip(etats, etats[1:]) if a != b]
pas = [(POS[b] - POS[a]) % 6 for a, b in transitions if a in POS and b in POS]
plus, moins = pas.count(1), pas.count(5)
sauts = len(transitions) - plus - moins            # etats non adjacents, 0 ou 7
part_sauts = sauts / len(transitions) if transitions else 0.0
if moe_vu:
    verdict = "INVALIDE : MOE = 1 pendant l'ecoute (le pont ne doit pas etre actif)"
elif part_sauts > PART_SAUTS_MAX and len(transitions) >= N_MIN:
    verdict = f"TROP RAPIDE : {part_sauts:.0%} de sauts (E001 a 50 Hz) : recommencer plus lentement"
elif plus + moins < N_MIN:
    verdict = f"NON CONCLUANT : {plus + moins} transitions adjacentes (< {N_MIN}) : rotor non entraine ou trop peu tourne"
elif plus > 2 * moins:
    verdict = "theta CROISSANT en tournant en arriere : hypothese CONFIRMEE (le walk pousse le moteur en arriere)"
elif moins > 2 * plus:
    verdict = "theta DECROISSANT en tournant en arriere : hypothese REFUTEE (theta croissant = avant physique)"
else:
    verdict = "MELANGE : sens non tranche (jeu, aller-retour de la main ?) : recommencer d'un seul geste"
bilan = dict(duree_s=DUREE, trames_e001=len(lignes), transitions=len(transitions), theta_plus=plus, theta_moins=moins,
             sauts=sauts, part_sauts=round(part_sauts, 3), etats_vus=sorted(set(etats)), moe_vu=moe_vu,
             defaut_final=defaut, verdict=verdict)
with open(PREFIXE + ".json", "w", encoding="utf-8") as f:
    json.dump(bilan, f, ensure_ascii=False, indent=1)
print(json.dumps(bilan, ensure_ascii=False))
