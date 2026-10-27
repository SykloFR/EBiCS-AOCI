"""Rejeu de la simulation du couple (porte 4, 27/10/2026) sur les tables REELLES du depot : REVERSE du build de banc
(inc/config.h), table de get_standstill_position (src/main.c, branche MP.reverse > 0 du build de banc et branche par
defaut), angles A5 (inc/bench_hall_angles.h), formule 6-step (src/main.c). Hors ligne, bibliotheque standard.
Modele (aoci-firmware-re/validation/porte-4/correctif-sens-analyse-20261027.md) : courant (0, i_q) a l'angle phi ->
attraction vers psi = phi - atan2(i_q, 0) ; couple avant = -sin(psi - theta_rotor) (theta decroissant = avant, t24).
Critere ecrit avant : couple avant >= 0,80 sur tout le secteur, pour les 6 etats, a l'arret et en 6-step.
Usage : python -I tools/bench/simu_sens_patch.py [racine_depot]   (code retour 0 = conforme)"""
import math, os, re, sys

R = sys.argv[1] if len(sys.argv) > 1 else "."
SEUIL = 0.80
lire = lambda p: open(os.path.join(R, p), encoding="utf-8", errors="replace").read()
cfg, main_c, ang = lire("inc/config.h"), lire("src/main.c"), lire("inc/bench_hall_angles.h")

m = re.search(r"#ifdef BENCH_BUILD\s*(?:/\*.*?\*/\s*)?#define REVERSE\s+(-?\d+)", cfg, re.S)
rev_banc = int(m.group(1)) if m else int(re.search(r"#define REVERSE\s+(-?\d+)", cfg).group(1))
deg = {f"Hall_{k}": int(v) * 360 / 2**32 % 360 for k, v in re.findall(r"#define BENCH_HALL_(\d\d)\s+\((-?\d+)\)", ang)}
if os.path.exists(os.path.join(R, "inc/bench_hall_rotation.h")):   # table en rotation prioritaire (main.c)
    deg = {f"Hall_{k}": int(v) * 360 / 2**32 % 360 for k, v in
           re.findall(r"#define BENCH_HALL_ROT_(\d\d)\s+\((-?\d+)\)", lire("inc/bench_hall_rotation.h"))}
f = main_c[main_c.index("void get_standstill_position(){"):]
f = f[:f.index("\n}\n")]
banc = re.search(r"if \(MP\.reverse > 0\) \{(.*?)return;", f, re.S)
table_banc = {int(e): h for e, h in re.findall(r"case (\d): q31_rotorposition_hall = (Hall_\d\d);", banc.group(1))} if banc else {}
reste = f[banc.end():] if banc else f
table_def = {int(e): h for e, h in re.findall(r"case (\d):\s*q31_rotorposition_hall = (Hall_\d\d);", reste)}
assert "q31_rotorposition_hall - MP.reverse * deg_30" in main_c, "formule 6-step introuvable"

# secteurs : frontiere Hall_xy = passage x -> y en theta croissant -> bord bas de y, bord haut de x
bas = {int(k[6]): a for k, a in deg.items()}
haut = {int(k[5]): a for k, a in deg.items()}


def avant(phi, i_q, th):
    return -math.sin(math.radians(phi - math.degrees(math.atan2(i_q, 0.0)) - th))


def pire(y, phi, i_q, n=61):
    lo, larg = bas[y], (haut[y] - bas[y]) % 360
    return min(avant(phi, i_q, lo + larg * k / (n - 1)) for k in range(n))


ok = True
print(f"REVERSE (build de banc) = {rev_banc:+d} ; angles A5 : { {k: round(v, 1) for k, v in sorted(deg.items())} }")
print(f"table a l'arret, branche MP.reverse > 0 : {table_banc or 'ABSENTE'} ; branche par defaut : {table_def}")
for rev in (rev_banc,):
    table = table_banc if rev > 0 and table_banc else table_def
    i_q = float(rev)
    print(f"\nwalk : i_q = {i_q:+.0f} ; etat | arret (pire cas) | 6-step apres un front dans le sens commande (pire cas)")
    for y in [4, 5, 1, 3, 2, 6]:
        phi_arret = deg[table[y]] - rev * 30.0
        phi_6 = (bas[y] if rev < 0 else haut[y]) - rev * 30.0
        a, s = pire(y, phi_arret, i_q), pire(y, phi_6, i_q)
        bon = a >= SEUIL and s >= SEUIL
        ok &= bon
        print(f"   {y}  | {a:+.2f} ({table[y]}) | {s:+.2f}  {'OK' if bon else 'NON CONFORME'}")
print(f"\nverdict : {'CONFORME' if ok else 'NON CONFORME'} (couple avant >= {SEUIL:.2f} partout)")
sys.exit(0 if ok else 1)
