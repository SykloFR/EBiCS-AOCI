"""Porte 4 : enregistrement en lecture seule des capteurs de courant pendant une remise sous tension (attach, sans reset).
Se reconnecte apres chaque coupure, journalise toutes les ~50 ms : sorties brutes ADC (batterie, phases), offsets
injectes, offset batterie, defaut, MOE, broches PB3/PB4/PB5. Permet de voir si les sorties derivent apres la calibration.
Usage : python tools/bench/t11_porte4_demarrage.py ELF [duree_s=90]"""
import os, sys, time
sys.path.insert(0, os.path.dirname(__file__))
import elfsym
from pyocd.core.helpers import ConnectHelper

ELF = sys.argv[1]
DUREE = float(sys.argv[2]) if len(sys.argv) > 2 else 90.0
V = ["adc_value[0]", "adc_value[4]", "adc_value[8]", "adc_value[7]", "bat_current_offset", "safety_fault_code_latched"]
sym = elfsym.resolve(ELF, V)
IOFF = (0x40012414, 0x40012814, 0x40013C14)


def rd(t, e):
    a, n = sym[e]
    return int.from_bytes(bytes(t.read_memory_block8(a, n)), "little")


t_debut = time.time()
dernier = None
while time.time() - t_debut < DUREE:
    try:
        with ConnectHelper.session_with_chosen_probe(target_override="stm32f103rc", connect_mode="attach",
                                                     options={"frequency": 1000000}) as s:
            t = s.target
            elfsym.verifie_image(t, ELF)
            print(f"{time.time() - t_debut:7.2f} s connexion", flush=True)
            while time.time() - t_debut < DUREE:
                v = [rd(t, e) for e in V]
                v[:4] = [round(x, -1) for x in v[:4]]     # arrondi a 10 points : on ne journalise pas le bruit
                io = [t.read32(a) & 0xFFF for a in IOFF]
                moe = (t.read32(0x40012C44) >> 15) & 1
                pb = (t.read32(0x40010C0C) >> 3) & 7
                ligne = (f"bat {v[0]:4d} ph3 {v[1]:4d} ph2 {v[2]:4d} ph1 {v[3]:4d} | IOFF {io} offbat {v[4]} | "
                         f"defaut {v[5]} MOE {moe} PB3-5 {pb:03b}")
                if ligne != dernier:      # seulement les changements (a 10 points pres, voir arrondi ci-dessous)
                    print(f"{time.time() - t_debut:7.2f} s {ligne}", flush=True)
                    dernier = ligne
                time.sleep(0.05)
    except Exception as ex:
        if dernier != "perdu":
            print(f"{time.time() - t_debut:7.2f} s liaison perdue ({type(ex).__name__})", flush=True)
            dernier = "perdu"
        time.sleep(0.2)
