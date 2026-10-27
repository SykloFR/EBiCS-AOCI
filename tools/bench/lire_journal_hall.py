"""Lecteur HORS LIGNE du journal des fronts Hall (build ebics-aoci 713f4dd, trame E20A, script t22). Bibliotheque standard.
Aucun acces materiel : lit le CSV ecrit par t22 (PREFIXE.csv) ou une liste JSON de trames E20A decodees
(bench_can_decode.decode : rang, cas, tics, six_step, sens, pwm, angle_deg, hj_seq).

Usage : python lire_journal_hall.py JOURNAL.csv|JOURNAL.json [--angles inc/bench_hall_angles.h] [--reverse 1|-1] [--detail]
        [--json]   (--reverse : MP.reverse du build lu ; -1 jusqu'a 713f4dd, +1 depuis le correctif de sens du 27/10)

CLASSES (une seule par passage, testees DANS CET ORDRE ; la premiere satisfaite l'emporte) :
  e  aucun front           : N < N_MIN (2 fronts journalises).
  c  hors sequence         : part des fronts hors sequence (etats non adjacents, x = y, etat 0 ou 7) >= 20 %,
                             OU part des fronts de duree < 0,10 ms >= 20 % (0,10 ms entre fronts = 1667 erps : aucun
                             rotor reel en walk ; filtre d'entree TIMER2 ~4 us, donc plancher ~2 tics).
  b  arriere dominants     : ratio arriere = arr / (av + arr) >= 0,65 ET avance nette <= -3 secteurs.
  d  avant reguliers       : ratio avant >= 0,80 ET avance nette >= +6 secteurs (un tour electrique).
  a  frontiere             : |avance nette| <= 2 secteurs ET allers-retours sur une meme frontiere >= 50 % des paires
                             de fronts valides consecutifs.
  Repli (aucun critere complet) : avance nette >= +3 -> d, <= -3 -> b, sinon a, marque « critere partiel ».
Conventions (relevé A5, BENCH_HALL_ORDER = -1) : sequence avant 6-4-5-1-3-2 (cas 64,45,51,13,32,26) = theta croissant,
sens detecte EBiCS = +1 ; arriere = cas 46,62,23,31,15,54, sens -1.
ATTENTION (comparaison avec le firmware d'origine, 27/10) : « avant » ici = sens EBiCS (theta croissant), PAS le sens
physique. Le firmware d'origine entraine en O 4-6-2-3-1-5 = E 1-5-4-6-2-3, c'est-a-dire theta DECROISSANT ; theta
croissant est l'arriere physique (CONFIRME par t24, 27/10). Avec --reverse 1 (defaut, builds corriges), les classes
b / d et l'avance nette sont comptees par rapport au sens COMMANDE (theta decroissant) ; avec -1, par rapport a theta
croissant. Les colonnes du CSV de t22 (avant / arriere) restent en sens EBiCS : lire les cas, pas l'etiquette.

LIMITES DU JOURNAL (verifiees dans le source, voir rapport) :
  - duree = capture TIMER2 (u16, 2 us, compteur 0xFFFF relance a chaque front) : un ecart > 131,07 ms est enregistre
    modulo 65536. Si le journal n'a pas tourne (N < 64), le rang 0 est le 1er front du passage : sa duree est NON FIABLE
    et exclue des statistiques de duree.
  - cas hors des 12 cas EBiCS : pas de « default » dans le switch -> sens et angle du journal sont ceux du front precedent
    (perimes), signales « perime ».
  - angle_deg = q31_rotorposition_hall (angle de FRONTIERE), tronque a 1,4 deg, pas l'angle applique au FOC. En 6-step,
    EBiCS applique frontiere - MP.reverse * 30 deg (REVERSE = -1 : +30 deg) quel que soit le sens ; l'angle interpole
    n'est pas reconstructible depuis le journal.
  - six_step (b0) = mode en vigueur A L'ARRIVEE du front (decide par l'ISR ADC avant ce front).
"""
import csv, json, math, os, re, sys

# ---------------- criteres chiffres (modifiables ici, nulle part ailleurs) ----------------
N_MIN = 2                    # e : moins de N_MIN fronts
DUREE_COURTE_MS = 0.10       # c : duree « tres courte »
PART_HORS_SEQ_C = 0.20       # c : part de fronts hors sequence
PART_COURTS_C = 0.20         # c : part de fronts tres courts
RATIO_ARRIERE_B = 0.65       # b
AVANCE_B = -3                # b : secteurs
RATIO_AVANT_D = 0.80         # d
AVANCE_D = 6                 # d : secteurs
AVANCE_A = 2                 # a : |avance nette| max
PART_AR_A = 0.50             # a : part d'allers-retours parmi les paires valides consecutives
TOL_ANGLE_DEG = 1.6          # angle journal vs frontiere attendue (troncature u8 = 1,4 deg)
REVERSE = 1                  # MP.reverse du build de banc depuis le 27/10 (config.h) ; --reverse -1 pour 713f4dd et avant

SEQ_AVANT = [6, 4, 5, 1, 3, 2]
AVANT = {SEQ_AVANT[i] * 10 + SEQ_AVANT[(i + 1) % 6] for i in range(6)}
ARRIERE = {(c % 10) * 10 + c // 10 for c in AVANT}
# angles de frontiere (deg) du build 6137f73 / 713f4dd, inc/bench_hall_angles.h ; remplaces par --angles
HALL_DEG = {64: 325.5, 45: 20.0, 51: 79.5, 13: 143.5, 32: 206.0, 26: 267.0}

NOMS = {"a": "frontiere (allers-retours, avance nette ~ 0)", "b": "fronts arriere dominants",
        "c": "hors sequence (non adjacents, 0/7, durees tres courtes)", "d": "avant reguliers (rotation puis defaut)",
        "e": "aucun front"}
LECTURE = {
    "a": ("Le rotor (ou le signal) bascule sur une frontiere sans passer au secteur suivant.",
          "Une variable : meme walk 5 pts, DUREE courte (t22 ... 5 0.4) pour figer les 1ers fronts ; puis, a part, "
          "vecteur fixe (t20/OL_SET) sur la frontiere vue, u_d par paliers, etat Hall E001 lu par CAN : "
          "le rotor franchit-il la frontiere sous champ statique ? Allers-retours < 1 ms = electrique, pas mecanique."),
    "b": ("Le couple pousse vers theta decroissant alors qu'EBiCS attend l'avant. Rotation reguliere + plateau immobile : "
          "moteur qui tourne a l'envers derriere la roue libre du pedalier (charge identique avec ou sans chaine).",
          "Une variable : sens PHYSIQUE de theta croissant, sans walk : vecteur fixe (OL_SET, u_d 56) balaye lentement "
          "theta croissant sur plusieurs tours electriques, chaine retiree, repere sur le plateau (video) ; le plateau "
          "avance -> theta croissant = avant ; immobile -> theta croissant = arriere (roue libre). Hors ligne : erreur "
          "6-step des fronts arriere (~60 deg par construction, colonne err6step)."),
    "c": ("Fronts parasites (commutation, couplage ou cablage du codeur), pas une rotation.",
          "Une variable : vecteur fixe immobile (OL_SET, u_d par paliers, rotor tenu par la chaine), etat Hall E001/E200 "
          "a 50 Hz : un changement d'etat sans mouvement = parasite ; puis MOE = 1 a u_d = 0 (PWM 50 %) seul."),
    "d": ("Le demarrage marche ; l'angle interpole (ou le passage 6-step -> interpole) derive avec la vitesse. "
          "Si le plateau n'a PAS tourne : theta croissant est l'arriere physique (moteur derriere la roue libre) ou "
          "fronts non mecaniques (codeur sensible au courant).",
          "Une variable : meme walk, DUREE courte (t22 ... 5 0.4) pour voir le debut (~30 fronts, tous gardes) ; si le "
          "plateau n'a pas tourne, test de sens physique de la classe b (balayage OL_SET theta croissant, plateau "
          "filme) ; hors ligne, reconstruire l'angle interpole depuis les durees (main.c:1963-1966)."),
    "e": ("Couple insuffisant (ou fronts trop lents pour l'erps).",
          "Une variable : rester a 5 pts et lire i_q reellement atteint (E000) et u_q (E001) ; ne pas monter le "
          "courant avant d'avoir vu i_q atteindre la consigne."),
}


def lire_angles(chemin):
    out = {}
    with open(chemin, encoding="utf-8", errors="replace") as f:
        for m in re.finditer(r"#define\s+BENCH_HALL_(\d\d)\s+\((-?\d+)\)", f.read()):
            out[int(m.group(1))] = (int(m.group(2)) % (1 << 32)) * 360.0 / (1 << 32)
    if len(out) != 6:
        sys.exit(f"angles incomplets dans {chemin} : {sorted(out)}")
    return out


def lire_journal(chemin):
    """Renvoie une liste de dicts : rang, cas, duree_ms, six_step, sens, angle_deg, pwm (None si absent)."""
    if chemin.lower().endswith(".json"):
        with open(chemin, encoding="utf-8") as f:
            data = json.load(f)
        if isinstance(data, dict):
            data = data.get("fronts", [])
        out = [dict(rang=int(v["rang"]), cas=int(v["cas"]),
                    duree_ms=float(v["duree_ms"]) if "duree_ms" in v else int(v["tics"]) * 0.002,
                    six_step=int(v["six_step"]), sens=int(v["sens"]), angle_deg=float(v["angle_deg"]),
                    pwm=v.get("pwm")) for v in data if int(v.get("rang", 0)) != 0xFF]
    else:
        with open(chemin, newline="", encoding="utf-8") as f:
            r = csv.DictReader(f)
            attendu = ["rang", "cas", "transition", "sens_sequence", "duree_ms", "six_step", "sens_detecte", "angle_deg"]
            if r.fieldnames is None or r.fieldnames[:8] != attendu:
                sys.exit(f"en-tete inattendu (format t22 attendu {attendu}) : {r.fieldnames}")
            out = [dict(rang=int(v["rang"]), cas=int(v["cas"]), duree_ms=float(v["duree_ms"]),
                        six_step=int(v["six_step"]), sens=int(v["sens_detecte"]), angle_deg=float(v["angle_deg"]),
                        pwm=int(v["pwm"]) if v.get("pwm") not in (None, "") else None) for v in r]
    return sorted(out, key=lambda v: v["rang"])


def ecart(a, b):
    """a - b ramene dans ]-180, 180]."""
    d = (a - b) % 360.0
    return d - 360.0 if d > 180.0 else d


def centre_secteur(etat, hall):
    i = SEQ_AVANT.index(etat)
    entree = SEQ_AVANT[i - 1] * 10 + etat
    sortie = etat * 10 + SEQ_AVANT[(i + 1) % 6]
    return (hall[entree] + ecart(hall[sortie], hall[entree]) / 2.0) % 360.0


def analyser(fr, hall):
    n = len(fr)
    res = dict(n=n)
    valides = [v for v in fr if v["cas"] in AVANT or v["cas"] in ARRIERE]
    av = sum(v["cas"] in AVANT for v in fr)
    ar = sum(v["cas"] in ARRIERE for v in fr)
    if REVERSE > 0:              # couple commande vers theta decroissant (avant physique, t24) : classes relatives au sens commande
        av, ar = ar, av
    hors = n - av - ar
    res.update(avant=av, arriere=ar, hors_sequence=hors, avance_nette=av - ar,
               tours_electriques=round((av - ar) / 6.0, 2),
               ratio_arriere=round(ar / (av + ar), 3) if av + ar else None,
               ratio_avant=round(av / (av + ar), 3) if av + ar else None,
               part_hors_sequence=round(hors / n, 3) if n else 0.0)
    # allers-retours : paires consecutives de fronts valides, meme frontiere, sens oppose (xy puis yx)
    paires = [(p["cas"], q["cas"]) for p, q in zip(fr, fr[1:]) if p in valides and q in valides]
    ar_n = sum(1 for p, q in paires if p % 10 == q // 10 and p // 10 == q % 10)
    res.update(allers_retours=ar_n, part_allers_retours=round(ar_n / len(paires), 3) if paires else 0.0)
    frontieres = sorted({min(c // 10, c % 10) * 10 + max(c // 10, c % 10) for c in (v["cas"] for v in valides)})
    etats = sorted({v["cas"] // 10 for v in fr} | {v["cas"] % 10 for v in fr})
    res.update(frontieres=frontieres, etats=etats)
    # durees : rang 0 exclu si le journal n'a pas tourne (1er front du passage, capture modulo 65536)
    fiables = [v for v in fr if not (v["rang"] == 0 and n < 64)]
    d = sorted(v["duree_ms"] for v in fiables)
    courts = [v for v in fiables if v["duree_ms"] < DUREE_COURTE_MS]
    res.update(rang0_exclu=(n > 0 and n < 64), duree_min_ms=d[0] if d else None,
               duree_mediane_ms=d[len(d) // 2] if d else None, courts=len(courts),
               part_courts=round(len(courts) / len(fiables), 3) if fiables else 0.0,
               ar_rapides_1ms=sum(1 for p, q in zip(fr, fr[1:]) if p in valides and q in valides
                                  and p["cas"] % 10 == q["cas"] // 10 and p["cas"] // 10 == q["cas"] % 10
                                  and q["duree_ms"] < 1.0))
    if len(fiables) >= 6:
        k = len(fiables) // 3
        m1 = sorted(v["duree_ms"] for v in fiables[:k])[k // 2]
        m3 = sorted(v["duree_ms"] for v in fiables[-k:])[k // 2]
        res["acceleration"] = round(m1 / m3, 2) if m3 > 0 else None   # > 1 : les fronts se rapprochent
    else:
        res["acceleration"] = None
    six = sum(v["six_step"] for v in fr)
    res.update(part_6step=round(six / n, 3) if n else None, part_interpole=round(1 - six / n, 3) if n else None,
               dernier_mode=("6-step" if fr[-1]["six_step"] else "interpole") if n else None)
    pw = [v["pwm"] for v in fr if v["pwm"] is not None]
    res["pwm_inactif"] = sum(1 for p in pw if not p) if pw else "non enregistre (t22 ne garde pas b3)"
    # coherence sens detecte / sequence, angle retenu / frontiere attendue, angle 6-step / centre du secteur
    sens_ko, ang_ko, perimes, err6 = 0, 0, 0, {"avant": [], "arriere": []}
    for i, v in enumerate(fr):
        c = v["cas"]
        if c in AVANT or c in ARRIERE:
            attendu_sens = 1 if c in AVANT else -1
            bord = c if c in AVANT else (c % 10) * 10 + c // 10
            v["angle_attendu"] = hall[bord]
            v["ecart_angle"] = round(ecart(v["angle_deg"], hall[bord]), 1)
            if v["sens"] != attendu_sens:
                sens_ko += 1
            if not (-TOL_ANGLE_DEG <= v["ecart_angle"] <= 0.3):
                ang_ko += 1
            applique = (v["angle_deg"] - REVERSE * 30.0) % 360.0
            v["err_6step"] = round(ecart(applique, centre_secteur(c % 10, hall)), 1)
            err6["avant" if c in AVANT else "arriere"].append(v["err_6step"])
        else:
            perimes += 1
            v["angle_attendu"] = None
            v["ecart_angle"] = "perime"
            v["err_6step"] = None
    res.update(sens_incoherents=sens_ko, angles_hors_tolerance=ang_ko, fronts_angle_perime=perimes,
               err_6step_max_avant=max((abs(x) for x in err6["avant"]), default=None),
               err_6step_max_arriere=max((abs(x) for x in err6["arriere"]), default=None))
    return res


def classer(r):
    n = r["n"]
    if n < N_MIN:
        return "e", f"N = {n} < {N_MIN}"
    if r["part_hors_sequence"] >= PART_HORS_SEQ_C or r["part_courts"] >= PART_COURTS_C:
        return "c", (f"hors sequence {r['part_hors_sequence']:.0%} (seuil {PART_HORS_SEQ_C:.0%}), "
                     f"< {DUREE_COURTE_MS} ms {r['part_courts']:.0%} (seuil {PART_COURTS_C:.0%})")
    ra, rv, net = r["ratio_arriere"] or 0.0, r["ratio_avant"] or 0.0, r["avance_nette"]
    if ra >= RATIO_ARRIERE_B and net <= AVANCE_B:
        return "b", f"ratio arriere {ra:.2f} >= {RATIO_ARRIERE_B}, avance nette {net} <= {AVANCE_B}"
    if rv >= RATIO_AVANT_D and net >= AVANCE_D:
        return "d", f"ratio avant {rv:.2f} >= {RATIO_AVANT_D}, avance nette {net} >= {AVANCE_D}"
    if abs(net) <= AVANCE_A and r["part_allers_retours"] >= PART_AR_A:
        return "a", f"|avance nette| {abs(net)} <= {AVANCE_A}, allers-retours {r['part_allers_retours']:.0%} >= {PART_AR_A:.0%}"
    cl = "d" if net >= 3 else ("b" if net <= -3 else "a")
    return cl, f"CRITERE PARTIEL (repli sur l'avance nette {net}) : ratio avant {rv:.2f}, allers-retours {r['part_allers_retours']:.0%}"


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not args:
        sys.exit(__doc__)
    hall = dict(HALL_DEG)
    if "--reverse" in sys.argv:
        global REVERSE
        REVERSE = int(sys.argv[sys.argv.index("--reverse") + 1])
        args = [a for a in args if a != sys.argv[sys.argv.index("--reverse") + 1]]
    if "--angles" in sys.argv:
        hall = lire_angles(sys.argv[sys.argv.index("--angles") + 1])
        args = [a for a in args if a != sys.argv[sys.argv.index("--angles") + 1]]
    fr = lire_journal(args[0])
    r = analyser(fr, hall)
    cl, pourquoi = classer(r)
    if "--json" in sys.argv:
        print(json.dumps(dict(fichier=os.path.basename(args[0]), classe=cl, critere=pourquoi, indicateurs=r),
                         ensure_ascii=False, indent=1))
        return
    print(f"journal {os.path.basename(args[0])} : {r['n']} fronts")
    if "--detail" in sys.argv and fr:
        print(" rang  cas  seq      ms     6s sens  angle  attendu  ecart  err6step")
        for v in fr:
            c = v["cas"]
            sq = "avant" if c in AVANT else ("arriere" if c in ARRIERE else "HORS")
            at = f"{v['angle_attendu']:7.1f}" if v["angle_attendu"] is not None else "      -"
            print(f"{v['rang']:5d}  {c // 10}->{c % 10} {sq:7s} {v['duree_ms']:8.3f} {v['six_step']:2d} {v['sens']:+4d} "
                  f"{v['angle_deg']:6.1f} {at} {str(v['ecart_angle']):>6s} {str(v['err_6step']):>8s}")
    if r["n"]:
        print(f"  avant {r['avant']}, arriere {r['arriere']}, hors sequence {r['hors_sequence']} ; avance nette "
              f"{r['avance_nette']} secteurs ({r['tours_electriques']:+} tour el.) ; ratio arriere {r['ratio_arriere']}")
        print(f"  allers-retours {r['allers_retours']} ({r['part_allers_retours']:.0%} des paires valides, dont "
              f"{r['ar_rapides_1ms']} en < 1 ms) ; frontieres {r['frontieres']} ; etats {r['etats']}")
        print(f"  duree min {r['duree_min_ms']} ms, mediane {r['duree_mediane_ms']} ms, < {DUREE_COURTE_MS} ms : "
              f"{r['courts']} ; acceleration (mediane 1er tiers / dernier tiers) {r['acceleration']}"
              f"{' ; rang 0 exclu (1er front, duree modulo 131 ms)' if r['rang0_exclu'] else ''}")
        print(f"  6-step {r['part_6step']:.0%}, interpole {r['part_interpole']:.0%}, dernier front en {r['dernier_mode']} ;"
              f" PWM inactif : {r['pwm_inactif']}")
        print(f"  sens detecte incoherent : {r['sens_incoherents']} ; angle retenu hors frontiere attendue : "
              f"{r['angles_hors_tolerance']} ; angle perime (cas hors sequence) : {r['fronts_angle_perime']}")
        print(f"  angle 6-step (frontiere {'+' if REVERSE < 0 else '-'}30) vs centre du secteur de l'etat : "
              f"|err| max {r['err_6step_max_avant']} deg sur fronts avant, {r['err_6step_max_arriere']} deg sur arriere")
    print(f"CLASSE {cl} : {NOMS[cl]}  [{pourquoi}]")
    print(f"  lecture : {LECTURE[cl][0]}")
    print(f"  prochaine mesure : {LECTURE[cl][1]}")


if __name__ == "__main__":
    main()
