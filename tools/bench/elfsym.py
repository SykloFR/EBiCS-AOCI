"""Resolution d'adresses depuis l'ELF du build (jamais d'adresse RAM codee en dur).
Utilise arm-none-eabi-gdb en mode batch sur l'ELF (DWARF, -g) : resolve(elf, ["MP.phase_current_max", ...])
-> {expr: (adresse, taille)}. Les registres peripheriques (adresses fixes du composant) restent dans les scripts."""
import os, re, struct, subprocess, sys

CRC_ADR, APP = 0x0803AFC0, 0x08005000


def verifie_image(t, elf):
    """Arrete le script si le CRC de l'appli en flash (0x0803AFC0) differe de celui de l'image construite a cote de l'ELF :
    sinon les adresses RAM lues seraient celles d'un autre build (26/10 : ELF reconstruit, image du 20/10 en flash)."""
    d = os.path.dirname(os.path.abspath(elf))
    img = next((os.path.join(d, f) for f in os.listdir(d) if f.endswith("_aoci_full.bin")), None)
    if img is None:
        sys.exit(f"ARRET : pas d'image *_aoci_full.bin a cote de {elf}")
    attendu = struct.unpack_from("<I", open(img, "rb").read(), CRC_ADR - APP)[0]
    lu = t.read32(CRC_ADR)
    if lu != attendu:
        sys.exit(f"ARRET : CRC en flash {lu:#010x}, ELF {elf} construit pour {attendu:#010x} : mauvais ELF")
    print(f"image en flash = build de l'ELF (CRC {lu:#010x})")


def resolve(elf, exprs):
    cmd = ["arm-none-eabi-gdb", "-batch", "-nx", elf]
    for e in exprs:
        cmd += ["-ex", f"print/x (unsigned long)&({e})", "-ex", f"print sizeof({e})"]
    out = subprocess.run(cmd, capture_output=True, text=True, check=True).stdout
    vals = re.findall(r"^\$\d+ = (\S+)", out, re.M)
    if len(vals) != 2 * len(exprs):
        raise RuntimeError("gdb n'a pas resolu toutes les expressions :\n" + out)
    return {e: (int(vals[2 * i], 16), int(vals[2 * i + 1], 0)) for i, e in enumerate(exprs)}
