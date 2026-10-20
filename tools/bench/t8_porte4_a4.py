"""Porte 4, A4 : premiere mise sous tension, alim de labo 24 V / 0,5 A, MOE = 0, moteur branche, rien ne doit commuter.
Lecture seule en attach, sans halt ni reset : MOE, defaut, cause du reset, offsets de courant calibres au demarrage,
courants de phase et batterie (moyenne, crete, bruit), tension batterie, sonde NTC PB1, etat Hall. Echantillonnage SWD ~N Hz.
Usage : python tools/bench/t8_porte4_a4.py ELF [duree_s] [V_alim_lue]"""
import os, statistics, sys, time
sys.path.insert(0, os.path.dirname(__file__))
import elfsym
from pyocd.core.helpers import ConnectHelper

ELF = sys.argv[1]
DUREE = float(sys.argv[2]) if len(sys.argv) > 2 else 10.0
V_ALIM = float(sys.argv[3]) if len(sys.argv) > 3 else None
BDTR, GPIOC_ISTAT = 0x40012C44, 0x40011008
IOFF = {"ADC0 (ph3)": 0x40012414, "ADC1 (ph2)": 0x40012814, "ADC2 (ph1)": 0x40013C14}
PTS_PAR_V = 59.1                     # tension batterie (analyse de l'origine, confirmee porte 3)
V = ["safety_fault_code_latched", "boot_reset_flags", "ui_8_PWM_ON_Flag", "i16_ph1_current", "i16_ph2_current",
     "i16_ph3_current", "MS.Battery_Current", "MS.Voltage", "voltage_raw_filtered", "adc_value[6]", "MS.int_Temperature",
     "ui8_hall_state", "bat_current_offset", "adc_value[0]", "MS.i_q_setpoint"]
SIGNES = {"i16_ph1_current", "i16_ph2_current", "i16_ph3_current", "MS.Battery_Current", "MS.Voltage",
          "MS.int_Temperature", "bat_current_offset", "MS.i_q_setpoint"}
sym = elfsym.resolve(ELF, V)
res = []


def rd(t, e):
    a, n = sym[e]
    return int.from_bytes(bytes(t.read_memory_block8(a, n)), "little", signed=e in SIGNES)


def verdict(nom, cond, detail):
    res.append(cond)
    print(f"[{'OK' if cond else 'ECHEC'}] {nom} : {detail}")


with ConnectHelper.session_with_chosen_probe(target_override="stm32f103rc", connect_mode="attach",
                                             options={"frequency": 1000000}) as s:
    t = s.target
    f = rd(t, "boot_reset_flags")
    print(f"cause du reset {f:#x}, defaut {rd(t, 'safety_fault_code_latched')}, offsets injectes : "
          + ", ".join(f"{k} {t.read32(a) & 0xFFF}" for k, a in IOFF.items()) + f", offset batterie {rd(t, 'bat_current_offset')}")
    ech = {e: [] for e in V}
    moe_vu, hall_vu = set(), set()
    t0 = time.time()
    while time.time() - t0 < DUREE:
        moe_vu.add((t.read32(BDTR) >> 15) & 1)
        hall_vu.add((t.read32(GPIOC_ISTAT) >> 6) & 7)
        for e in V:
            ech[e].append(rd(t, e))
    n = len(ech["i16_ph1_current"])
print(f"{n} echantillons en {DUREE:.0f} s ({n / DUREE:.0f} Hz par grandeur)")

verdict("MOE = 0 pendant toute la mesure", moe_vu == {0}, f"valeurs vues {sorted(moe_vu)}")
verdict("aucun defaut, demarrage par mise sous tension", ech["safety_fault_code_latched"][-1] == 0 and (f >> 27) & 1 == 1,
        f"defaut {ech['safety_fault_code_latched'][-1]}, cause {f:#x}")
for e in ("i16_ph1_current", "i16_ph2_current", "i16_ph3_current"):
    x = ech[e]
    m, lo, hi = statistics.mean(x), min(x), max(x)
    verdict(f"{e} centre et sans bruit", abs(m) <= 2 and hi - lo <= 8,
            f"moyenne {m:+.2f} pts, min {lo}, max {hi}, crete-crete {hi - lo} (critere |moy| <= 2, cc <= 8 ; trip a 16)")
ib = ech["MS.Battery_Current"]
verdict("courant batterie au repos", abs(statistics.mean(ib)) <= 150,
        f"moyenne {statistics.mean(ib):+.0f} mA, min {min(ib)}, max {max(ib)} (ADC brut moyen {statistics.mean(ech['adc_value[0]']):.0f})")
vr = statistics.mean(ech["voltage_raw_filtered"])
v_calc = vr / PTS_PAR_V
detail = f"brut {vr:.0f} pts -> {v_calc:.2f} V a 59,1 pts/V ; MS.Voltage {statistics.mean(ech['MS.Voltage']) / 1000:.2f} V"
if V_ALIM:
    verdict("tension batterie vs alim (+-2 %)", abs(v_calc - V_ALIM) / V_ALIM <= 0.02, detail + f" ; alim {V_ALIM:.2f} V")
else:
    print(f"[INFO] tension batterie : {detail} (passer la tension lue sur l'alim en 3e argument pour le verdict)")
ntc = ech["adc_value[6]"]
verdict("sonde NTC PB1 valide (50..4000)", 50 <= min(ntc) and max(ntc) <= 4000,
        f"ADC {min(ntc)}..{max(ntc)}, T {statistics.mean(ech['MS.int_Temperature']):.0f} degC (constantes NTC M820 : indicatif)")
verdict("etat Hall valide (ni 0 ni 7)", not (hall_vu & {0, 7}), f"etats GPIOC vus {sorted(hall_vu)}, ui8_hall_state {sorted(set(ech['ui8_hall_state']))}")
verdict("aucune consigne", set(ech["MS.i_q_setpoint"]) == {0} and set(ech["ui_8_PWM_ON_Flag"]) == {0},
        f"i_q_setpoint {sorted(set(ech['MS.i_q_setpoint']))}, PWM_ON {sorted(set(ech['ui_8_PWM_ON_Flag']))}")
print(f"\nBILAN A4 : {sum(res)}/{len(res)} OK")
