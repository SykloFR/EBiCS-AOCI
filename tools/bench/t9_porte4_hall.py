"""Porte 4 : lecture seule en attach des entrees du codeur magnetique (UVW lus comme Hall sur PC6-8), MOE = 0.
Releve les etats vus et la suite des changements pendant que le rotor est tourne a la main, dans les deux conventions :
  EBiCS   : (GPIOC_ISTAT >> 6) & 7, soit PC6 = bit 0, PC7 = bit 1, PC8 = bit 2 ;
  origine : PC6 << 2 | PC7 | PC8 << 1 (ordre attendu 4 6 2 3 1 5 dans un sens).
Usage : python tools/bench/t9_porte4_hall.py [duree_s]"""
import collections, sys, time
from pyocd.core.helpers import ConnectHelper

DUREE = float(sys.argv[1]) if len(sys.argv) > 1 else 30.0
BDTR, GPIOC_ISTAT = 0x40012C44, 0x40011008


def origine(e):
    pc6, pc7, pc8 = e & 1, (e >> 1) & 1, (e >> 2) & 1
    return pc6 << 2 | pc7 | pc8 << 1


with ConnectHelper.session_with_chosen_probe(target_override="stm32f103rc", connect_mode="attach",
                                             options={"frequency": 4000000}) as s:
    t = s.target
    suite, compte, moe_vu, n = [], collections.Counter(), set(), 0
    t0 = time.time()
    while time.time() - t0 < DUREE:
        e = (t.read32(GPIOC_ISTAT) >> 6) & 7
        moe_vu.add((t.read32(BDTR) >> 15) & 1)
        compte[e] += 1
        n += 1
        if not suite or suite[-1] != e:
            suite.append(e)
print(f"{n} lectures en {DUREE:.0f} s, MOE vus {sorted(moe_vu)}")
print("etats EBiCS vus  :", dict(sorted(compte.items())))
print("etats origine vus:", dict(sorted((origine(k), v) for k, v in compte.items())))
print(f"{len(suite) - 1} changements ; suite EBiCS  :", " ".join(map(str, suite[:60])))
print("                  suite origine:", " ".join(str(origine(e)) for e in suite[:60]))
print("etats invalides (0 ou 7) :", sum(compte[k] for k in (0, 7)), "lectures")
