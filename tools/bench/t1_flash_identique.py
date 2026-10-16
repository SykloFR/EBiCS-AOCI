"""Test 1 (lecture seule) : la flash de la carte est-elle toujours identique au dump de la porte 1 ?"""
import hashlib, sys
from pathlib import Path
from pyocd.core.helpers import ConnectHelper

ref = Path(sys.argv[1]).read_bytes()
with ConnectHelper.session_with_chosen_probe(target_override="stm32f103rc", connect_mode="attach",
                                             options={"frequency": 1000000}) as s:
    t = s.target
    bdtr = t.read32(0x40012C44)
    print("TIM1 BDTR = 0x%08X, MOE = %d" % (bdtr, (bdtr >> 15) & 1))
    data = bytes(t.read_memory_block8(0x08000000, 0x40000))
print("SHA-256 flash carte :", hashlib.sha256(data).hexdigest())
print("SHA-256 dump1.bin   :", hashlib.sha256(ref).hexdigest())
print("IDENTIQUE" if data == ref else "DIFFERENT : %d octets" % sum(a != b for a, b in zip(data, ref)))
