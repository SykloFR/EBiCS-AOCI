"""Resolution d'adresses depuis l'ELF du build (jamais d'adresse RAM codee en dur).
Utilise arm-none-eabi-gdb en mode batch sur l'ELF (DWARF, -g) : resolve(elf, ["MP.phase_current_max", ...])
-> {expr: (adresse, taille)}. Les registres peripheriques (adresses fixes du composant) restent dans les scripts."""
import re, subprocess


def resolve(elf, exprs):
    cmd = ["arm-none-eabi-gdb", "-batch", "-nx", elf]
    for e in exprs:
        cmd += ["-ex", f"print/x (unsigned long)&({e})", "-ex", f"print sizeof({e})"]
    out = subprocess.run(cmd, capture_output=True, text=True, check=True).stdout
    vals = re.findall(r"^\$\d+ = (\S+)", out, re.M)
    if len(vals) != 2 * len(exprs):
        raise RuntimeError("gdb n'a pas resolu toutes les expressions :\n" + out)
    return {e: (int(vals[2 * i], 16), int(vals[2 * i + 1], 0)) for i, e in enumerate(exprs)}
