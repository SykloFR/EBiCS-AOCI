"""A5 : relevé des angles du codeur (sorties U/V/W lues comme des Hall sur PC6-8) avec l'outil vecteur fixe piloté par CAN.
Fonctionne sans SWD (moteur monté sur le vélo, home trainer). Build de banc T2 + garde de calibration, alim limitée.

Méthode (celle de hall_detect de VESC) : une tension u_d est imposée à un angle électrique θ qui tourne lentement ; le rotor
suit. On relève l'état Hall (trame E001) à chaque pas, en montée puis en descente de θ. Chaque transition x -> y est vue
dans les deux sens ; la moyenne des deux angles annule le retard dû à la charge. Les 6 angles de transition donnent les
valeurs Hall_xy d'EBiCS (q31, 2^32 = 360°, angle du rotor au passage de l'état x à l'état y, numérotation EBiCS
PC6 = bit 0, PC7 = bit 1, PC8 = bit 2) et le signe de i32_hall_order.

Arrêts automatiques : défaut ou MOE retombé, calibration non faite ou état Hall invalide (0 / 7) au départ, courant batterie
>= 400 mA, aucune transition (le rotor ne suit pas). L'outil est toujours arrêté à la fin (OL_STOP, puis heartbeat coupé).

Usage : python tools/bench/t18_releve_angles_codeur.py SORTIE_PREFIXE [--ud 40] [--pas 2] [--tours 2] [--dt 0.04]
                [--entete inc/bench_hall_angles.h]   (écrit l'en-tête des angles si le verdict est OK)
        python tools/bench/t18_releve_angles_codeur.py SORTIE_PREFIXE --analyser FICHIER.csv   (analyse hors ligne)"""
import argparse, csv, json, os, sys, time
sys.path.insert(0, os.path.dirname(__file__))

AVANT = [6, 4, 5, 1, 3, 2]            # séquence « avant » d'EBiCS (cases 64, 45, 51, 13, 32, 26 du TIMER2_IRQHandler)
NOMS = ["Hall_64", "Hall_45", "Hall_51", "Hall_13", "Hall_32", "Hall_26"]
DEFAUT_EBICS = {"Hall_13": 1825361405, "Hall_32": -1789569490, "Hall_26": -966367405, "Hall_64": -322122295,
                "Hall_45": 381775140, "Hall_51": 1169185830}


def origine(e):
    """Numérotation de l'origine AOCI (PC6 << 2 | PC7 | PC8 << 1) à partir de celle d'EBiCS."""
    return (e & 1) << 2 | (e >> 1 & 1) | (e >> 2 & 1) << 1


def q31(deg):
    v = int(round((deg % 360) * 2**32 / 360)) & 0xFFFFFFFF
    return v - 2**32 if v >= 2**31 else v


def transitions(lignes):
    """lignes = [(sens, theta_deg, hall)] ; renvoie {(x, y): [theta, ...]} pour chaque sens."""
    t = {"montee": {}, "descente": {}}
    prec = {}
    for sens, theta, h in lignes:
        if h in (0, 7):
            continue
        p = prec.get(sens)
        if p is not None and h != p[1]:
            t[sens].setdefault((p[1], h), []).append((p[0] + theta) / 2)     # milieu entre les deux pas
        prec[sens] = (theta, h)
    return t


def moyenne_angles(a):
    import cmath, math
    z = sum(cmath.exp(1j * math.radians(x)) for x in a) / len(a)
    return math.degrees(cmath.phase(z)) % 360


def analyser(lignes):
    import math
    t = transitions(lignes)
    rapport = {"transitions_montee": {f"{x}->{y}": [round(v % 360, 1) for v in l] for (x, y), l in t["montee"].items()},
               "transitions_descente": {f"{x}->{y}": [round(v % 360, 1) for v in l] for (x, y), l in t["descente"].items()}}
    paires_avant = list(zip(AVANT, AVANT[1:] + AVANT[:1]))
    montee_avant = sum(len(t["montee"].get(p, [])) for p in paires_avant)
    montee_arriere = sum(len(t["montee"].get((y, x), [])) for (x, y) in paires_avant)
    if montee_avant == montee_arriere == 0:
        rapport["verdict"] = "ECHEC : aucune transition valide en montée (le rotor ne suit pas ou codeur muet)"
        return rapport
    sens_avant = montee_avant >= montee_arriere          # θ croissant = séquence avant d'EBiCS ?
    rapport["theta_croissant_parcourt"] = "séquence avant EBiCS 6-4-5-1-3-2" if sens_avant else "séquence arrière EBiCS 2-3-1-5-4-6"
    rapport["i32_hall_order"] = -1 if sens_avant else 1   # case 64 & co : direction = -i32_hall_order, à rendre positive
    angles = {}
    for nom, (x, y) in zip(NOMS, paires_avant):
        m = t["montee"].get((x, y) if sens_avant else (y, x), [])
        d = t["descente"].get((y, x) if sens_avant else (x, y), [])
        if not m or not d:
            angles[nom] = None
            continue
        am, ad = moyenne_angles(m), moyenne_angles(d)
        ecart = (am - ad + 180) % 360 - 180
        angles[nom] = {"montee": round(am, 1), "descente": round(ad, 1), "hysteresis": round(ecart, 1),
                       "angle": round(moyenne_angles([am, ad]), 1)}
    rapport["angles"] = angles
    ok = [a for a in angles.values() if a]
    if len(ok) == 6:
        seq = [angles[n]["angle"] for n in NOMS]
        # écart entre transitions successives dans le sens où θ les parcourt (sens arrière : angles décroissants)
        pas = [round(((b - a) if sens_avant else (a - b)) % 360, 1) for a, b in zip(seq, seq[1:] + seq[:1])]
        rapport["ecarts_successifs_deg"] = pas
        rapport["q31"] = {n: q31(angles[n]["angle"]) for n in NOMS}
        rapport["q31_defaut_ebics_M820"] = DEFAUT_EBICS
        rapport["origine_aoci"] = {n: f"{origine(int(n[5]))}->{origine(int(n[6]))}" for n in NOMS}
        bons = all(40 <= p <= 80 for p in pas) and all(abs(a["hysteresis"]) <= 30 for a in ok)
        rapport["verdict"] = "OK : 6 transitions, écarts 40-80°, hystérésis <= 30°" if bons else \
            "A REVOIR : écarts ou hystérésis hors tolérance (charge, pas trop rapide, codeur)"
    else:
        rapport["verdict"] = f"INCOMPLET : {len(ok)}/6 transitions vues dans les deux sens"
    return rapport


def mesurer(args):
    from bench_can_cmd import Banc, STATUTS
    lignes, arret = [], None
    with Banc() as b:
        time.sleep(1.2)
        e205, e200 = b.dernier.get(0xE205, {}), b.dernier.get(0xE200, {})
        print(f"départ : défaut {e200.get('defaut')}, calibration faite {e205.get('calibration_faite')}, "
              f"Hall valide {e205.get('hall_valide')}, roue arrêtée {e205.get('roue_arretee')}", flush=True)
        if e200.get("defaut") or not e205.get("calibration_faite") or not e205.get("hall_valide"):
            sys.exit("ARRET : défaut, calibration non faite ou codeur muet (état Hall 0 ou 7)")
        b.demarrer_heartbeat(); time.sleep(0.8)
        st = b.ol_set(0, args.ud)
        if st != 0:
            sys.exit(f"ARRET : OL_SET refusé : {STATUTS.get(st, st)}")
        time.sleep(1.0)                                   # alignement du rotor sur θ = 0
        n = int(round(360 * args.tours / args.pas))
        t_dep = time.monotonic()
        plan = [("montee", i * args.pas) for i in range(n + 1)] + [("descente", (n - i) * args.pas) for i in range(n + 1)]
        try:
            for sens, theta in plan:
                st = b.ol_set(theta, args.ud)
                time.sleep(args.dt)
                e0, e1, e101 = b.dernier.get(0xE000, {}), b.dernier.get(0xE001, {}), b.dernier.get(0xE101, {})
                lignes.append((sens, theta, e1.get("hall_gpio"), e0.get("id"), e0.get("iq"), e101.get("ibat"),
                               round(time.monotonic() - t_dep, 3)))
                if st != 0 or not e0.get("moe") or b.dernier.get(0xE200, {}).get("defaut"):
                    # MOE retombe : la E200 qui porte le defaut arrive apres la E000 ; None = indetermine
                    d = b.dernier.get(0xE200, {}).get("defaut") or b.defaut_apres(b.t_moe0 or time.monotonic() - 0.05)
                    arret = f"θ {theta}° : statut {st}, MOE {e0.get('moe')}, défaut {'indéterminé' if d is None else d}"
                    break
                if (e101.get("ibat") or 0) >= 0.4:
                    arret = f"θ {theta}° : courant batterie {e101.get('ibat')} A"
                    break
                if len(lignes) % 45 == 0:
                    print(f"{sens} θ {theta:5.0f}° Hall {e1.get('hall_gpio')} i_d {e0.get('id')} Ibat {e101.get('ibat')}", flush=True)
        finally:
            print("OL_STOP :", STATUTS.get(b.ol_stop(), "pas de réponse"), flush=True)
    return lignes, arret


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("prefixe")
    ap.add_argument("--ud", type=int, default=40)
    ap.add_argument("--pas", type=float, default=2.0)
    ap.add_argument("--tours", type=float, default=2.0)
    ap.add_argument("--dt", type=float, default=0.04)
    ap.add_argument("--analyser")
    ap.add_argument("--entete", help="écrire inc/bench_hall_angles.h si le verdict est OK")
    a = ap.parse_args()
    if a.analyser:
        with open(a.analyser, encoding="utf-8") as f:
            lignes = [(r["sens"], float(r["theta"]), int(r["hall"])) for r in csv.DictReader(f) if r["hall"] not in ("", "None")]
        arret = None
    else:
        brut, arret = mesurer(a)
        with open(a.prefixe + ".csv", "w", newline="", encoding="utf-8") as f:
            w = csv.writer(f); w.writerow(["sens", "theta", "hall", "id", "iq", "ibat", "t_s"]); w.writerows(brut)
        lignes = [(s, t, h) for s, t, h, *_ in brut if h is not None]
    r = analyser(lignes)
    r["arret_anticipe"] = arret
    with open(a.prefixe + ".json", "w", encoding="utf-8") as f:
        json.dump(r, f, ensure_ascii=False, indent=1)
    if a.entete:
        if not str(r.get("verdict", "")).startswith("OK"):
            sys.exit(f"en-tête NON écrit : verdict {r.get('verdict')}")
        q = r["q31"]
        lignes_h = ["/* Angles du codeur relevés sur le moteur monté (A5), écrits par tools/bench/t18_releve_angles_codeur.py.",
                    f"   Source : {os.path.basename(a.prefixe)}.json. Numérotation EBiCS (PC6 = bit 0). Ne pas éditer à la main. */",
                    "#ifndef BENCH_HALL_ANGLES_H_", "#define BENCH_HALL_ANGLES_H_", "#define BENCH_HALL_MESURE 1",
                    f"#define BENCH_HALL_ORDER ({r['i32_hall_order']})"]
        lignes_h += [f"#define BENCH_HALL_{n[5:]} ({q[n]})   /* {r['angles'][n]['angle']} deg */" for n in NOMS]
        lignes_h += ["#endif", ""]
        with open(a.entete, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(lignes_h))
        print(f"en-tête écrit : {a.entete}")
    print(json.dumps(r, ensure_ascii=False, indent=1))


if __name__ == "__main__":
    main()
