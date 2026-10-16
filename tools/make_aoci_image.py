"""Prépare l'image de l'application EBiCS portée pour le bootloader AOCI.

Le bootloader AOCI (0x08000000-0x08004FFF) ne lance l'appli que si le CRC32 matériel
STM32 (poly 0x04C11DB7, init 0xFFFFFFFF, mots de 32 bits, sans inversion) calculé sur
0x08005000-0x0803AFBF est égal au mot stocké à 0x0803AFC0 (voir aoci-firmware-re/bootloader/README.md).
Le CRC porte sur toute la zone, y compris le remplissage 0xFF après le code : il faut donc
que cette zone soit entièrement effacée avant l'écriture.

Sortie : une image complète 0x08005000-0x0803AFFF (0x36000 octets) = code + 0xFF + CRC + 0xFF.
Écrite telle quelle, elle efface et réécrit toutes les pages de la zone appli (rien en dessous de 0x08005000,
rien à partir de 0x0803B000 : angles Hall appris et configuration restent intacts).

Usage : python tools/make_aoci_image.py build-m3/ebics.bin build-m3/ebics_aoci_full.bin
        python tools/make_aoci_image.py --verify dump.bin     (vérifie un dump de flash complet base 0x08000000, ou une image complète de 0x36000 octets)
"""
import struct
import sys
from pathlib import Path

APP_BASE = 0x08005000
CRC_ADDR = 0x0803AFC0
APP_LEN = CRC_ADDR - APP_BASE          # 0x35FC0, zone couverte par le CRC
FULL_LEN = 0x0803B000 - APP_BASE       # 0x36000, image complète à écrire
RAM_BASE, RAM_END = 0x20000000, 0x2000C000   # 48 Ko


def stm32_crc(buf: bytes) -> int:
    crc = 0xFFFFFFFF
    for (word,) in struct.iter_unpack("<I", buf):
        crc ^= word
        for _ in range(32):
            crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if crc & 0x80000000 else (crc << 1) & 0xFFFFFFFF
    return crc


def check_vectors(app: bytes) -> list:
    """Contrôles de cohérence de la table de vecteurs ; retourne la liste des problèmes."""
    problems = []
    sp, reset = struct.unpack_from("<II", app, 0)
    if not (RAM_BASE < sp <= RAM_END):
        problems.append(f"SP initial 0x{sp:08X} hors de la RAM")
    if not (reset & 1):
        problems.append(f"vecteur de reset 0x{reset:08X} sans bit Thumb")
    if not (APP_BASE <= (reset & ~1) < CRC_ADDR):
        problems.append(f"vecteur de reset 0x{reset:08X} hors de la zone appli")
    return problems


def build(src: Path, dst: Path) -> None:
    app = src.read_bytes()
    if len(app) > APP_LEN:
        sys.exit(f"ERREUR : image de {len(app)} octets > zone appli ({APP_LEN})")
    problems = check_vectors(app)
    if problems:
        sys.exit("ERREUR : " + " ; ".join(problems))
    area = app + b"\xFF" * (APP_LEN - len(app))
    crc = stm32_crc(area)
    full = area + struct.pack("<I", crc) + b"\xFF" * (FULL_LEN - APP_LEN - 4)
    assert len(full) == FULL_LEN
    dst.write_bytes(full)
    print(f"code {len(app)} octets ({100 * len(app) / APP_LEN:.1f} % de la zone), CRC32 0x{crc:08X} -> 0x{CRC_ADDR:08X}")
    print(f"image complète {FULL_LEN:#x} octets à écrire à 0x{APP_BASE:08X} : {dst}")


def verify(dump: Path) -> None:
    d = dump.read_bytes()
    base = APP_BASE if len(d) == FULL_LEN else 0x08000000   # image complete (0x36000) ou dump de flash entier
    area = d[APP_BASE - base:CRC_ADDR - base]
    stored = struct.unpack_from("<I", d, CRC_ADDR - base)[0]
    calc = stm32_crc(area)
    print(f"CRC calculé 0x{calc:08X}, stocké 0x{stored:08X} :", "OK" if calc == stored else "FAUX")
    for p in check_vectors(area):
        print("ATTENTION :", p)
    sys.exit(0 if calc == stored else 1)


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--verify":
        verify(Path(sys.argv[2]))
    elif len(sys.argv) == 3:
        build(Path(sys.argv[1]), Path(sys.argv[2]))
    else:
        sys.exit(__doc__)
