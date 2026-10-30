"""Fenetre de calme de la mesure des courants (porte 4, revue Fangyan du 30/10/2026, point A3).
Sur les anneaux figes E20B/E20C/E20D des essais en pic : controle de coherence des anneaux (rangs 0 a 127 une fois et une
seule, sans valeurs contradictoires), puis, pour chaque lecture de voie brute (E20D), erreur par rapport a l'ajustement
sinusoidal de l'anneau (comme etablissement_mesure.py) et distance de la fin de l'echantillon a la commutation la plus
proche AVANT elle, parmi les trois phases et pour toutes les combinaisons (rapports S(k-1) / S(k-2), declenchement du
build). Commutations : bras haut qui s'ouvre (s) et bras bas qui se ferme (s + DTG) en montee ; bras bas qui s'ouvre
(2T - s) et bras haut qui se ferme (2T - s + DTG) en descente.
Resultat : histogramme des distances pour les lectures fausses (> 40 pts ou butee) dont la propre voie est etablie (marge
>= DYN_STAB, donc non expliquees par A1), et la plus petite fenetre couvrant 99 % d'entre elles.
Usage : python -I tools/bench/calme_mesure.py [dossier des captures] [capture.csv=ebics|r1|r2|r3 ...]"""
import csv, ast, math, sys

T, SQ3, TH0, AV, DEF, TE, DTG = 2000, 28, 1810, 65, 1990, 45, 25
STAB, STAB_DYN = 450, 350


def svpwm(a, b):
    Ua = (SQ3 * T * a) >> 4; Ub = -T * b; X = Ub; Y = (Ua + Ub) >> 1; Z = (Ub - Ua) >> 1; s = [0, 0, 0]
    if (Y >= 0 and Z < 0 and X > 0) or (Y < 0 and Z >= 0 and X <= 0):
        s[0] = ((T + X - Z) >> 12) + (T >> 1); s[1] = s[0] + (Z >> 11); s[2] = s[1] - (X >> 11)
    if (Y >= 0 and Z >= 0) or (Y < 0 and Z < 0):
        s[0] = ((T + Y - Z) >> 12) + (T >> 1); s[1] = s[0] + (Z >> 11); s[2] = s[0] - (Y >> 11)
    if (Y < 0 and Z >= 0 and X > 0) or (Y >= 0 and Z < 0 and X <= 0):
        s[0] = ((T + Y - X) >> 12) + (T >> 1); s[2] = s[0] - (Y >> 11); s[1] = s[2] + (X >> 11)
    return s


def sw(ud, uq, deg):
    th = math.radians(deg); c, sn = math.cos(th), math.sin(th)
    return svpwm(int(round(ud * c + uq * sn)), int(round(-ud * sn + uq * c)))


def anneaux(f):
    """anneaux figes et pleins : {seq: {trame: {rang: champs}}}, et liste des incoherences (rang vu avec deux valeurs)"""
    R, incoh = {}, []
    for r in csv.reader(open(f, encoding='utf-8', errors='replace')):
        if len(r) < 4 or r[1] not in ('E20B', 'E20C', 'E20D'):
            continue
        try:
            d = ast.literal_eval(r[3])
        except Exception:
            continue
        if d.get('figee') != 1 or d.get('pleine') != 1 or d.get('rang', 255) == 255:
            continue
        g = R.setdefault(d['pt_seq'], {}).setdefault(r[1], {})
        cle = {k: v for k, v in d.items() if k not in ('rang',)}
        if d['rang'] in g and g[d['rang']] != cle:
            incoh.append((d['pt_seq'], r[1], d['rang']))
        g[d['rang']] = cle
    return R, incoh


def imax(s): return max(range(3), key=lambda p: (s[p], -p))


def robuste(sp, sn, th):
    mp, mn = imax(sp), imax(sn); dyn = sp[mp] > th or sn[mn] > th; m = mp if dyn else mn
    if dyn and mp != mn: return DEF
    return (min(sp[m], sn[m]) - AV) if dyn else DEF


def ebics(s):
    m = imax(s); return s[m] - AV if s[m] > TH0 else DEF


def marge(s, p, cc4):
    tech = cc4 + TE
    if tech > 2 * T - s[p]: return -1
    return tech - (s[p] + DTG)


def distance_commutation(s, tech):
    """distance (ticks) de la fin de l'echantillon a la commutation la plus proche avant elle, toutes phases"""
    d = 10 ** 6
    for p in range(3):
        for e in (s[p], s[p] + DTG, 2 * T - s[p], 2 * T - s[p] + DTG):
            if e <= tech: d = min(d, tech - e)
    return d


D = sys.argv[1].rstrip('/') + '/' if len(sys.argv) > 1 else '../aoci-firmware-re/validation/porte-4/'
FICH = {'perf-niveau5-12h21.csv': 'ebics', 'perf-niveau5-12h21-2.csv': 'ebics', 'perf-niveau5-12h21-3.csv': 'r1',
        'perf-niveau5-15h19.csv': 'r2'}
if len(sys.argv) > 2: FICH = {k: v for k, v in (x.split('=') for x in sys.argv[2:])}

faux_hors_a1, faux_tous, bons, n_anneaux = [], [], [], 0
for fn, mode in FICH.items():
    R, incoh = anneaux(D + fn)
    pleins = [s for s, g in R.items() if all(len(g.get(k, {})) == 128 for k in ('E20B', 'E20C', 'E20D'))]
    print(f"{fn} : anneaux figes pleins {len(pleins)} sur {len(R)}, rangs 0-127 complets ; valeurs contradictoires : {len(incoh)}")
    for seq in pleins:
        g = R[seq]; c = g['E20C']; a = g['E20D']
        if sorted(c) != list(range(128)): print('   rangs incomplets', seq); continue
        S = {k: sw(c[k]['ud'] * 1.0, c[k]['uq'] * 1.0, c[k]['angle_deg']) for k in c if c[k].get('foc')}
        rows = [k for k in sorted(c) if all(j in S for j in (k - 1, k - 2, k - 3))]
        if len(rows) < 60: continue
        M = {}
        for k in rows:
            if mode == 'ebics': combos = [(S[k - 1], ebics(S[k - 2])), (S[k - 2], ebics(S[k - 2]))]
            elif mode == 'r1': cc = robuste(S[k - 2], S[k - 1], TH0); combos = [(S[k - 1], cc), (S[k - 2], cc)]
            else:
                th = 1560 if mode == 'r3' else TH0
                c1 = robuste(S[k - 2], S[k - 1], th); c2 = robuste(S[k - 3], S[k - 2], th)
                combos = [(S[k - 2], c2), (S[k - 2], c1), (S[k - 1], c1)]
            M[k] = combos
        best = None
        for sgn in (1, -1):
            A, B = [], []
            for k in rows:
                th = math.radians(c[k]['angle_deg'])
                for p in range(3):
                    if min(marge(s, p, cc) for s, cc in M[k]) < 700 or abs(a[k]['av'][p]) >= 127: continue
                    ang = th - sgn * p * 2 * math.pi / 3; A.append((math.cos(ang), -math.sin(ang))); B.append(a[k]['av'][p])
            if len(A) < 40: continue
            sxx = sum(x * x for x, y in A); syy = sum(y * y for x, y in A); sxy = sum(x * y for x, y in A)
            bx = sum(x * b for (x, y), b in zip(A, B)); by = sum(y * b for (x, y), b in zip(A, B)); det = sxx * syy - sxy * sxy
            re_ = (bx * syy - by * sxy) / det; im = (by * sxx - bx * sxy) / det
            res = (sum((b - (x * re_ + y * im)) ** 2 for (x, y), b in zip(A, B)) / len(A)) ** .5
            if best is None or res < best[0]: best = (res, sgn, re_, im)
        if best is None or abs(complex(best[2], best[3])) < 40: continue
        n_anneaux += 1
        _, sgn, re_, im = best
        for k in rows:
            th = math.radians(c[k]['angle_deg'])
            for p in range(3):
                pred = re_ * math.cos(th - sgn * p * 2 * math.pi / 3) - im * math.sin(th - sgn * p * 2 * math.pi / 3)
                v = a[k]['av'][p]
                faux = abs(v) >= 127 or abs(v - pred) > 40
                mg = min(marge(s, p, cc) for s, cc in M[k])
                if mg < 0: continue                                  # voie non conductrice : lecture sans objet
                st = min(STAB if cc == DEF else STAB_DYN for s, cc in M[k])
                dist = min(distance_commutation(s, cc + TE) for s, cc in M[k])
                (faux_tous if faux else bons).append(dist)
                if faux and mg >= st: faux_hors_a1.append(dist)

print(f"anneaux ajustes : {n_anneaux} ; lectures fausses : {len(faux_tous)} dont {len(faux_hors_a1)} avec la voie etablie (hors A1) ; justes : {len(bons)}")
bornes = [0, 25, 50, 75, 100, 125, 150, 180, 220, 300, 450, 10 ** 6]
for nom, v in (('fausses hors A1', faux_hors_a1), ('fausses toutes', faux_tous), ('justes', bons)):
    if not v: print(nom, ': aucune'); continue
    h = [sum(1 for x in v if bornes[i] <= x < bornes[i + 1]) for i in range(len(bornes) - 1)]
    print(f"{nom:16s} distance a la commutation la plus proche (ticks) : " +
          ' '.join(f"[{bornes[i]}-{bornes[i + 1] if bornes[i + 1] < 10 ** 6 else '...'}[ {h[i]}" for i in range(len(h))))
    s = sorted(v)
    print(f"{'':16s} 99 % sous {s[min(len(s) - 1, int(0.99 * len(s)))]} ticks ; 90 % sous {s[int(0.9 * (len(s) - 1))]} ; mediane {s[len(s) // 2]}")
