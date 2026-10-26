"""Porte 4, A5 sans codeur : outil vecteur fixe en boucle ouverte (build de banc), MOE = 1 pendant l'essai.
Rampe de u_d par paliers a angle fixe, lecture des courants a chaque palier, arret au premier seuil atteint :
  moyenne d'une phase >= SEUIL pts, plus de 20 % des echantillons du palier >= 15 pts (le bruit de mesure fait des pics
  isoles a 15-16 pts, A4 ; un pic court reel est couvert par le trip du firmware, 16 pts sur 6 periodes),
  courant batterie >= 400 mA, defaut verrouille ou MOE retombe.
L'outil est arrete dans tous les cas (bench_ol_enable = 0) ; sans rafraichissement, il s'arrete seul en 2 s (keepalive).
Attendu a l'angle 0 (u_alpha = u_d) : ph1 > 0, ph2 et ph3 ~ -ph1/2, i_d > 0 et croissant avec u_d, i_q ~ 0.
Usage : python tools/bench/t10_porte4_vecteur.py ELF [ud_max=60] [pas=2] [seuil_pts=10] [angle_deg=0]"""
import os, statistics, sys, time
sys.path.insert(0, os.path.dirname(__file__))
import elfsym
from pyocd.core.helpers import ConnectHelper

ELF = sys.argv[1]
UD_MAX = int(sys.argv[2]) if len(sys.argv) > 2 else 60
PAS = int(sys.argv[3]) if len(sys.argv) > 3 else 2
SEUIL = int(sys.argv[4]) if len(sys.argv) > 4 else 10
ANGLE = round(float(sys.argv[5]) / 360 * 2**32) if len(sys.argv) > 5 else 0
ANGLE = ANGLE - 2**32 if ANGLE >= 2**31 else ANGLE
PALIER_S, ECH_MAX, IBAT_MAX = 0.5, 15, 400
BDTR = 0x40012C44
V = ["safety_fault_code_latched", "ui_8_PWM_ON_Flag", "bench_ol_enable", "bench_ol_active", "bench_ol_keepalive",
     "bench_ol_ud", "bench_ol_angle", "i16_ph1_current", "i16_ph2_current", "i16_ph3_current", "MS.i_d", "MS.i_q",
     "MS.u_d", "MS.Battery_Current", "MS.i_q_setpoint"]
SIGNES = {"bench_ol_ud", "bench_ol_angle", "i16_ph1_current", "i16_ph2_current", "i16_ph3_current", "MS.i_d", "MS.i_q",
          "MS.u_d", "MS.Battery_Current", "MS.i_q_setpoint"}
sym = elfsym.resolve(ELF, V)


def rd(t, e):
    a, n = sym[e]
    return int.from_bytes(bytes(t.read_memory_block8(a, n)), "little", signed=e in SIGNES)


def wr(t, e, v):
    a, n = sym[e]
    t.write_memory_block8(a, list(int(v).to_bytes(n, "little", signed=v < 0)))
    t.flush()


def moe(t):
    return (t.read32(BDTR) >> 15) & 1


paliers, arret = [], None
with ConnectHelper.session_with_chosen_probe(target_override="stm32f103rc", connect_mode="attach",
                                             options={"frequency": 4000000}) as s:
    t = s.target
    depart = {e: rd(t, e) for e in ("safety_fault_code_latched", "bench_ol_active", "MS.i_q_setpoint", "ui_8_PWM_ON_Flag")}
    print(f"depart : MOE {moe(t)}, {depart}")
    if moe(t) or any(depart.values()):
        sys.exit("ARRET : MOE, defaut, outil actif ou consigne non nuls au depart")
    try:
        wr(t, "bench_ol_ud", 0); wr(t, "bench_ol_angle", ANGLE); wr(t, "bench_ol_keepalive", 8000); wr(t, "bench_ol_enable", 1)
        time.sleep(0.1)
        if not (moe(t) and rd(t, "bench_ol_active")):
            arret = f"activation refusee : MOE {moe(t)}, outil actif {rd(t, 'bench_ol_active')}, defaut {rd(t, 'safety_fault_code_latched')}"
        ud = 0
        while arret is None and ud < UD_MAX:
            ud = min(ud + PAS, UD_MAX)
            wr(t, "bench_ol_ud", ud); wr(t, "bench_ol_keepalive", 8000)
            ech = {e: [] for e in ("i16_ph1_current", "i16_ph2_current", "i16_ph3_current", "MS.i_d", "MS.i_q", "MS.Battery_Current")}
            t0, n_pic = time.time(), 0
            while time.time() - t0 < PALIER_S:
                for e in ech:
                    ech[e].append(rd(t, e))
                f, m = rd(t, "safety_fault_code_latched"), moe(t)
                n_pic += max(abs(ech[e][-1]) for e in ("i16_ph1_current", "i16_ph2_current", "i16_ph3_current")) >= ECH_MAX
                if f or not m:
                    arret = f"defaut {f}, MOE {m} a u_d {ud}"
                    break
            n = len(ech["MS.i_d"])
            moy = {e: statistics.mean(x) for e, x in ech.items()}
            et = max(statistics.pstdev(ech[e]) for e in ("i16_ph1_current", "i16_ph2_current", "i16_ph3_current"))
            paliers.append((ud, moy, n))
            print(f"u_d {ud:3d} (MS.u_d {rd(t, 'MS.u_d'):3d}) : ph1 {moy['i16_ph1_current']:+6.2f} ph2 {moy['i16_ph2_current']:+6.2f} "
                  f"ph3 {moy['i16_ph3_current']:+6.2f} pts | i_d {moy['MS.i_d']:+7.2f} i_q {moy['MS.i_q']:+7.2f} | "
                  f"Ibat {moy['MS.Battery_Current']:+5.0f} mA | ecart-type max {et:4.1f} | pics >= {ECH_MAX} : {n_pic}/{n}")
            if arret is None and n_pic > 0.2 * n:
                arret = f"{n_pic}/{n} echantillons >= {ECH_MAX} pts a u_d {ud}"
            if arret is None:
                ph_max = max(abs(moy[e]) for e in ("i16_ph1_current", "i16_ph2_current", "i16_ph3_current"))
                if ph_max >= SEUIL:
                    arret = f"seuil de {SEUIL} pts atteint a u_d {ud} ({ph_max:.1f} pts, ~{ph_max / 3.14:.1f} A)"
                elif moy["MS.Battery_Current"] >= IBAT_MAX:
                    arret = f"courant batterie {moy['MS.Battery_Current']:.0f} mA a u_d {ud}"
    finally:
        wr(t, "bench_ol_ud", 0)
        wr(t, "bench_ol_enable", 0)
        time.sleep(0.1)
        fin = f"fin : MOE {moe(t)}, outil actif {rd(t, 'bench_ol_active')}, defaut {rd(t, 'safety_fault_code_latched')}, PWM_ON {rd(t, 'ui_8_PWM_ON_Flag')}"
print("arret :", arret or f"rampe complete jusqu'a u_d {UD_MAX}")
print(fin)
if len(paliers) >= 2:
    p1 = [p[1]["i16_ph1_current"] for p in paliers]
    print("ph1 croissant avec u_d :", all(b >= a - 1 for a, b in zip(p1, p1[1:])))
    ud, moy, _ = paliers[-1]
    if abs(moy["i16_ph1_current"]) >= 2:
        print(f"dernier palier : ph2/ph1 {moy['i16_ph2_current'] / moy['i16_ph1_current']:+.2f}, "
              f"ph3/ph1 {moy['i16_ph3_current'] / moy['i16_ph1_current']:+.2f} (attendu -0,50), "
              f"somme {moy['i16_ph1_current'] + moy['i16_ph2_current'] + moy['i16_ph3_current']:+.2f} pts")
