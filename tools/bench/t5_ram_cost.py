"""Test 5 : cout en cycles (PI x2 + racine) en flottant et en entiers, mesure sur le Cortex-M3 de la carte.
Calcul pur en RAM : aucune broche, aucun timer, aucune ecriture flash. Usage : python tools/bench/t5_ram_cost.py
"""
import struct, subprocess, sys, time
from pathlib import Path
from pyocd.core.helpers import ConnectHelper

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
RT = HERE / "ramtest"
RES, LOAD = 0x2000B000, 0x20002000


def build():
    inc = ["port_f10x", "revue-puissance/proposed/inc", "Firmware_F10x/CMSIS", "Firmware_F10x/CMSIS/GD/GD32F10x/Include",
           "Firmware_F10x/GD32F10x_standard_peripheral/Include"]
    lib = "Firmware_F10x/GD32F10x_standard_peripheral/Source/"
    srcs = [str(RT / "ramtest_cost.c"), str(ROOT / "port_f10x/system_aoci.c"),
            str(ROOT / "revue-puissance/proposed/src/foc_int.c")] + \
           [str(ROOT / lib / ("gd32f10x_%s.c" % m)) for m in ("rcu", "misc")]
    cmd = ["arm-none-eabi-gcc", "-mcpu=cortex-m3", "-mthumb", "-mfloat-abi=soft", "-DGD32F10X_HD",
           "-DUSE_STDPERIPH_DRIVER", "-DHXTAL_VALUE=12000000U"]
    cmd += ["-I" + str(ROOT / i) for i in inc]
    cmd += ["-Os", "-std=gnu99", "-ffunction-sections", "-fdata-sections"] + srcs
    cmd += ["-nostartfiles", "-Wl,-T" + str(RT / "ramtest.ld"), "-Wl,--gc-sections", "--specs=nano.specs",
            "--specs=nosys.specs", "-lm", "-o", str(RT / "ramtest_cost.elf")]
    subprocess.run(cmd, check=True)
    subprocess.run(["arm-none-eabi-objcopy", "-O", "binary", str(RT / "ramtest_cost.elf"), str(RT / "ramtest_cost.bin")], check=True)
    return (RT / "ramtest_cost.bin").read_bytes()


def main():
    image = build()
    print("image %d octets" % len(image))
    with ConnectHelper.session_with_chosen_probe(target_override="stm32f103rc", connect_mode="attach",
                                                 options={"frequency": 1000000}) as s:
        t = s.target
        if (t.read32(0x40012C44) >> 15) & 1:
            print("ABANDON : TIM1 MOE = 1"); return 2
        t.halt()
        t.write_memory_block8(RES, bytes(64))
        t.write_memory_block8(LOAD, list(image))
        t.write_core_register("sp", 0x2000B800)
        t.write_core_register("xpsr", 0x01000000)
        t.write_core_register("pc", LOAD | 1)
        t.resume()
        deadline = time.time() + 20
        while time.time() < deadline and not t.is_halted():
            time.sleep(0.01)
        if not t.is_halted():
            t.halt(); print("ECHEC : pas termine")
        d = bytes(t.read_memory_block8(RES, 44))
    magic, sysclk, fmin, fmax, fsum, imin, imax, isum, n, chk_f, chk_i = struct.unpack("<11I", d)
    print("magic 0x%08X, horloge %d Hz, %d iterations" % (magic, sysclk, n))
    print("FLOTTANT (code d'origine) : min %5d  moyen %7.1f  max %5d cycles" % (fmin, fsum / n, fmax))
    print("ENTIER   (proposition P4) : min %5d  moyen %7.1f  max %5d cycles" % (imin, isum / n, imax))
    budget = sysclk / 16500.0
    print("budget par periode PWM (16,5 kHz) : %.0f cycles ; le flottant seul en prend %.0f %% (pire cas), l'entier %.0f %%" %
          (budget, 100.0 * fmax / budget, 100.0 * imax / budget))
    return 0


if __name__ == "__main__":
    rc = main()
    with ConnectHelper.session_with_chosen_probe(target_override="stm32f103rc", connect_mode="attach",
                                                 options={"frequency": 1000000}) as s:
        s.target.reset()
    print("carte relancee (reset)")
    sys.exit(rc)
