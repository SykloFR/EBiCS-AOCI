"""Sens physique de theta par l'actionneur (porte 4, 27/10/2026), apres t23 (capteur, lecture apres coup : theta croissant
quand on tourne le plateau en arriere). Outil vecteur fixe (OL_SET) qui TOURNE lentement en boucle ouverte, sans le
codeur dans la boucle : le rotor suit le champ. Une seule variable : le sens de rotation de theta (defaut : decroissant).

Montage : chaine retiree, alim limitee, coupure a portee, personne sur le velo, mains loin du plateau. Avant l'essai,
faire un repere (craie, adhesif) sur le plateau en face d'un repere fixe du cadre.

Resultat attendu, ecrit avant l'essai (hypothese « theta croissant = arriere physique », piste n 1 du 27/10) :
  theta DECROISSANT (--sens moins) -> le moteur tourne dans le sens avant du firmware d'origine (E 1-5-4-6-2-3), sa roue
  libre de sortie se bloque : le PLATEAU AVANCE (sens du pedalage) d'environ TOURS x 360 / (37,7 x 7) deg, soit ~27 deg
  pour 20 tours electriques (reducteur 37,7:1, 7 paires de poles, config.h).
  Cote codeur (automatique) : transitions Hall theta decroissant >= 80 % des transitions adjacentes, et au moins 60 % du
  nombre attendu (6 par tour electrique) ; sinon « le rotor ne suit pas » et l'essai ne conclut rien.
Lecture :
  - rotor suit + plateau avance de ~27 deg  -> piste CONFIRMEE (theta croissant = arriere : le walk pousse en arriere)
  - rotor suit + plateau immobile           -> piste REFUTEE (theta decroissant = arriere physique)
  - rotor ne suit pas, ou arret anticipe    -> NON CONCLUANT : on s'arrete et on revoit (pas de nouvel essai d'office)
L'observation du plateau est notee par l'operateur (aucun capteur sur le plateau sans chaine) et reportee dans le JSON.

Arrets automatiques : defaut ou MOE retombe (defaut lu dans une E200 recue apres), statut OL_SET refuse, courant
batterie >= 0,4 A, etat Hall 0 / 7, duree maximale. OL_STOP toujours envoye a la fin (puis heartbeat coupe).
Usage : python tools/bench/t24_sens_boucle_ouverte.py PREFIXE [--sens moins|plus] [--ud 56] [--tours 20] [--pas 4]
        [--dt 0.02]"""
import argparse, csv, json, os, sys, time
sys.path.insert(0, os.path.dirname(__file__))
from bench_can_cmd import Banc, STATUTS  # noqa: E402

SEQ = [6, 4, 5, 1, 3, 2]                  # theta croissant (A5, velo-F4-A5-angles-ud56.json)
POS = {e: i for i, e in enumerate(SEQ)}
POLES, REDUCTEUR = 7, 37.7                # config.h (14 aimants), reducteur AOCI Takamori mesure
PART_SENS_MIN, PART_NOMBRE_MIN = 0.80, 0.60
IBAT_MAX = 0.4


def analyser(halls, sens, tours):
    """halls = etats Hall successifs (E001) ; renvoie le bilan codeur et son verdict"""
    etats = [h for h in halls if h is not None]
    trans = [(a, b) for a, b in zip(etats, etats[1:]) if a != b]
    pas = [(POS[b] - POS[a]) % 6 for a, b in trans if a in POS and b in POS]
    plus, moins = pas.count(1), pas.count(5)
    sauts = len(trans) - plus - moins
    voulu, contre = (moins, plus) if sens == "moins" else (plus, moins)
    attendu = 6 * tours
    part_sens = voulu / (plus + moins) if plus + moins else 0.0
    suit = part_sens >= PART_SENS_MIN and voulu >= PART_NOMBRE_MIN * attendu
    return dict(transitions=len(trans), theta_plus=plus, theta_moins=moins, sauts=sauts, attendu=attendu,
                part_sens_voulu=round(part_sens, 3), rotor_suit=suit,
                verdict_codeur=("le rotor SUIT le champ" if suit else
                                "le rotor NE SUIT PAS (ou trop peu) : essai NON CONCLUANT"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("prefixe")
    ap.add_argument("--sens", choices=["moins", "plus"], default="moins")
    ap.add_argument("--ud", type=int, default=56)
    ap.add_argument("--tours", type=float, default=20)
    ap.add_argument("--pas", type=float, default=4)
    ap.add_argument("--dt", type=float, default=0.02)
    a = ap.parse_args()
    if not 0 < a.ud <= 60 or not 0 < a.tours <= 40 or not 0 < a.pas <= 10:
        sys.exit("ARRET : u_d 1..60 (BENCH_OL_UD_MAX), tours 1..40, pas 1..10 deg")
    signe = -1 if a.sens == "moins" else 1
    n = int(round(360 * a.tours / a.pas))
    duree_max = n * (a.dt + 0.06) + 5
    plateau = a.tours * 360 / (POLES * REDUCTEUR)
    print(f"theta {'DECROISSANT' if signe < 0 else 'CROISSANT'}, u_d {a.ud}, {a.tours:g} tours electriques "
          f"(plateau : ~{plateau:.0f} deg si la roue libre se bloque), {n} pas de {a.pas:g} deg", flush=True)
    lignes, arret = [], None
    with Banc() as b:
        t_ok = time.monotonic()
        e200, e205 = b.attendre_frais(0xE200, apres=t_ok, timeout=0.5), b.attendre_frais(0xE205, apres=t_ok, timeout=1.5)
        print(f"depart : defaut {e200 and e200.get('defaut')}, calibration {e205 and e205.get('calibration_faite')}, "
              f"Hall valide {e205 and e205.get('hall_valide')}", flush=True)
        if not e200 or not e205 or e200.get("defaut") or not e205.get("calibration_faite") or not e205.get("hall_valide"):
            sys.exit("ARRET : E200/E205 absentes, defaut, calibration non faite ou etat Hall invalide")
        b.demarrer_heartbeat(); time.sleep(0.8)
        st = b.ol_set(0, a.ud)
        if st != 0:
            sys.exit(f"ARRET : OL_SET refuse : {STATUTS.get(st, st)}")
        time.sleep(1.0)                                   # alignement du rotor sur theta = 0
        t_dep = time.monotonic()
        try:
            for k in range(n + 1):
                theta = signe * k * a.pas
                st = b.ol_set(theta, a.ud)
                time.sleep(a.dt)
                e0, e1, e101 = b.dernier.get(0xE000, {}), b.dernier.get(0xE001, {}), b.dernier.get(0xE101, {})
                h = e1.get("hall_gpio")
                lignes.append((round(time.monotonic() - t_dep, 3), round(theta % 360, 1), h, e0.get("id"), e0.get("iq"),
                               e101.get("ibat"), round((b.age(0xE001) or 0) * 1000)))
                if st != 0 or not e0.get("moe") or b.dernier.get(0xE200, {}).get("defaut"):
                    d = b.dernier.get(0xE200, {}).get("defaut") or b.defaut_apres(b.t_moe0 or time.monotonic() - 0.05)
                    arret = f"pas {k} : statut {st}, MOE {e0.get('moe')}, defaut {'indetermine' if d is None else d}"
                    break
                if (e101.get("ibat") or 0) >= IBAT_MAX:
                    arret = f"pas {k} : courant batterie {e101.get('ibat')} A"
                    break
                if h in (0, 7):
                    arret = f"pas {k} : etat Hall invalide {h}"
                    break
                if time.monotonic() - t_dep > duree_max:
                    arret = f"pas {k} : duree maximale {duree_max:.0f} s"
                    break
                if k % 90 == 0:
                    print(f"pas {k:5d}/{n} theta {theta % 360:5.0f} Hall {h} i_d {e0.get('id')} Ibat {e101.get('ibat')}",
                          flush=True)
        finally:
            t_stop = time.monotonic()
            print("OL_STOP :", STATUTS.get(b.ol_stop(), "pas de reponse"), flush=True)
            d_fin = b.defaut_apres(t_stop, timeout=0.5)

    with open(a.prefixe + ".csv", "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["t_s", "theta_deg", "hall_gpio", "id", "iq", "ibat", "age_e001_ms"])
        w.writerows(lignes)
    bilan = analyser([l[2] for l in lignes], a.sens, a.tours if arret is None else len(lignes) * a.pas / 360)
    bilan.update(sens=a.sens, ud=a.ud, tours=a.tours, pas=a.pas, dt=a.dt, pas_faits=len(lignes),
                 duree_s=lignes[-1][0] if lignes else 0, plateau_attendu_deg=round(plateau, 1),
                 ibat_max=max((l[5] or 0 for l in lignes), default=0), arret_anticipe=arret,
                 defaut_final="indetermine" if d_fin is None else d_fin, observation_plateau=None)
    bilan["verdict"] = ("NON CONCLUANT : arret anticipe" if arret else
                        "NON CONCLUANT : le rotor ne suit pas" if not bilan["rotor_suit"] else
                        "rotor suit : conclure sur l'observation du plateau (avance = piste confirmee, immobile = refutee)")
    with open(a.prefixe + ".json", "w", encoding="utf-8") as f:
        json.dump(bilan, f, ensure_ascii=False, indent=1)
    print(json.dumps(bilan, ensure_ascii=False))
    print("A NOTER PAR L'OPERATEUR : le repere du plateau a-t-il AVANCE (sens du pedalage), RECULE ou est-il reste "
          "IMMOBILE ? De combien environ ?")


if __name__ == "__main__":
    main()
