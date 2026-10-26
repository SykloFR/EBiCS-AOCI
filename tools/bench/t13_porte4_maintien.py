"""Porte 4 : maintien de l'outil vecteur fixe a u_d constant pendant N secondes (MOE = 1), pour une mesure au multimetre
(tension moyenne des phases par rapport au moins batterie : ~Vbus / 2 a u_d = 0 si le pont commute).
Rafraichit le keepalive toutes les 0,5 s, affiche courants, courant batterie et MOE chaque seconde. Arret immediat sur
defaut, MOE retombe, moyenne de phase >= SEUIL pts ou courant batterie >= 400 mA ; outil toujours arrete a la fin.
Usage : python tools/bench/t13_porte4_maintien.py ELF [ud=0] [duree_s=30] [seuil_pts=10] [angle_deg=0]"""
import os, statistics, sys, time
sys.path.insert(0, os.path.dirname(__file__))
import elfsym
from pyocd.core.helpers import ConnectHelper

ELF = sys.argv[1]
UD = int(sys.argv[2]) if len(sys.argv) > 2 else 0
DUREE = float(sys.argv[3]) if len(sys.argv) > 3 else 30.0
SEUIL = int(sys.argv[4]) if len(sys.argv) > 4 else 10
ANGLE = round(float(sys.argv[5]) / 360 * 2**32) if len(sys.argv) > 5 else 0
ANGLE = ANGLE - 2**32 if ANGLE >= 2**31 else ANGLE
BDTR = 0x40012C44
V = ["safety_fault_code_latched", "ui_8_PWM_ON_Flag", "bench_ol_enable", "bench_ol_active", "bench_ol_keepalive",
     "bench_ol_ud", "bench_ol_angle", "i16_ph1_current", "i16_ph2_current", "i16_ph3_current", "MS.Battery_Current",
     "MS.i_q_setpoint"]
SIGNES = {"bench_ol_ud", "bench_ol_angle", "i16_ph1_current", "i16_ph2_current", "i16_ph3_current", "MS.Battery_Current",
          "MS.i_q_setpoint"}
PH = ("i16_ph1_current", "i16_ph2_current", "i16_ph3_current")
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


arret = None
with ConnectHelper.session_with_chosen_probe(target_override="stm32f103rc", connect_mode="attach",
                                             options={"frequency": 4000000}) as s:
    t = s.target
    elfsym.verifie_image(t, ELF)
    if moe(t) or rd(t, "safety_fault_code_latched") or rd(t, "bench_ol_active") or rd(t, "MS.i_q_setpoint"):
        sys.exit("ARRET : MOE, defaut, outil actif ou consigne non nuls au depart")
    try:
        wr(t, "bench_ol_ud", UD); wr(t, "bench_ol_angle", ANGLE); wr(t, "bench_ol_keepalive", 8000); wr(t, "bench_ol_enable", 1)
        time.sleep(0.1)
        print(f"maintien u_d {UD}, angle {ANGLE / 2**32 * 360:.0f} deg, {DUREE:.0f} s : MOE {moe(t)}", flush=True)
        t0 = time.time()
        while arret is None and time.time() - t0 < DUREE:
            wr(t, "bench_ol_keepalive", 8000)
            ech = {e: [] for e in PH + ("MS.Battery_Current",)}
            t1 = time.time()
            while time.time() - t1 < 1.0:
                for e in ech:
                    ech[e].append(rd(t, e))
                f, m = rd(t, "safety_fault_code_latched"), moe(t)
                if f or not m:
                    arret = f"defaut {f}, MOE {m}"
                    break
                if time.time() - t1 > 0.5:
                    wr(t, "bench_ol_keepalive", 8000)
            moy = {e: statistics.mean(x) for e, x in ech.items()}
            print(f"{time.time() - t0:5.1f} s : ph1 {moy[PH[0]]:+6.2f} ph2 {moy[PH[1]]:+6.2f} ph3 {moy[PH[2]]:+6.2f} pts | "
                  f"Ibat {moy['MS.Battery_Current']:+5.0f} mA | MOE {moe(t)}", flush=True)
            if arret is None and max(abs(moy[e]) for e in PH) >= SEUIL:
                arret = f"seuil de {SEUIL} pts atteint"
            if arret is None and moy["MS.Battery_Current"] >= 400:
                arret = "courant batterie >= 400 mA"
    finally:
        wr(t, "bench_ol_ud", 0)
        wr(t, "bench_ol_enable", 0)
        time.sleep(0.1)
        fin = f"fin : MOE {moe(t)}, outil actif {rd(t, 'bench_ol_active')}, defaut {rd(t, 'safety_fault_code_latched')}"
print("arret :", arret or "duree atteinte")
print(fin)
