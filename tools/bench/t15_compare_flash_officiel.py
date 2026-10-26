"""Apres un flash par le CAN : relit par SWD (attach, lecture seule) la zone appli et la compare a un fichier de mise a jour
AOCI (en-tete de 16 octets 5C A0 82 00 ... CRKXY2, puis l'image a partir de 0x08005000, CRC32 STM32 a 0x0803AFC0).
Verifie aussi que le bootloader (0x08000000-0x08004FFF) et les pages 0x0803B000 / 0x0803F000 n'ont pas change.
Usage : python tools/bench/t15_compare_flash_officiel.py FICHIER_AOCI.bin|IMAGE_aoci_full.bin DOSSIER_PREUVES"""
import hashlib, os, sys
from pyocd.core.helpers import ConnectHelper

FICHIER, OUT = sys.argv[1], sys.argv[2]
APP, BOOT, BOOT_LEN = 0x08005000, 0x08000000, 0x5000
REF = {0x0803B000: ("hall_0803B000.bin", 256), 0x0803F000: ("page_0803F000.bin", 2048)}
f = open(FICHIER, "rb").read()
if len(f) == 0x36000:                      # image complete du build (*_aoci_full.bin) : image + CRC + 0xFF
    corps = f[:0x35FC4]
elif f[:4] == bytes.fromhex("5ca08200") and f[10:16] == b"CRKXY2":
    corps = f[16:]
else:
    sys.exit("ARRET : ni fichier AOCI (5C A0 82 00 ... CRKXY2) ni image complete de 0x36000 octets")
print(f"fichier {FICHIER} : {len(f)} octets, image {len(corps):#x} octets, CRC du fichier "
      f"{int.from_bytes(corps[0x0803AFC0 - APP:0x0803AFC4 - APP], 'little'):#010x}" if len(corps) >= 0x35FC4 else "")
with ConnectHelper.session_with_chosen_probe(target_override="stm32f103rc", connect_mode="attach",
                                             options={"frequency": 1000000}) as s:
    t = s.target
    print(f"MOE {(t.read32(0x40012C44) >> 15) & 1}, CRC en flash {t.read32(0x0803AFC0):#010x}")
    lu = bytes(t.read_memory_block8(APP, len(corps)))
    boot = bytes(t.read_memory_block8(BOOT, BOOT_LEN))
    pages = {a: bytes(t.read_memory_block8(a, n)) for a, (_, n) in REF.items()}
print(f"zone appli relue {len(lu):#x} octets : {'IDENTIQUE au fichier' if lu == corps else 'DIFFERENTE'}")
if lu != corps:
    diff = [i for i in range(len(corps)) if lu[i] != corps[i]]
    print(f"  {len(diff)} octets differents, premier a {APP + diff[0]:#010x}, dernier a {APP + diff[-1]:#010x}")
h = hashlib.sha256(boot).hexdigest()
print(f"bootloader SHA-256 {h[:16]}... -> {'inchange (porte 1 : 0adfbab1...547842)' if h.startswith('0adfbab1') and h.endswith('547842') else 'MODIFIE'}")
for a, (nom, _) in REF.items():
    ref = open(os.path.join(OUT, nom), "rb").read()
    print(f"{a:#010x} {'inchangee' if pages[a] == ref else 'MODIFIEE'} par rapport a {nom}")
