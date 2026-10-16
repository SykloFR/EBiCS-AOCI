"""Test 2 : essai en RAM de SystemInit() du portage + lecture ADC / GPIO, sans ecriture flash.

Prerequis : carte alimentee par le WCH-Link seul (pas de batterie), TIM1 MOE = 0.
Deroulement : controle MOE = 0 -> arret du coeur -> chargement en RAM -> execution (~2 s) -> lecture des resultats
              -> reset de la carte (le firmware d'origine redemarre).
Usage : python tools/bench/t2_ram_clock_adc.py
"""
import subprocess
import sys
import time
from pathlib import Path

from pyocd.core.helpers import ConnectHelper

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
RT = HERE / "ramtest"
RES = 0x2000B000
LOAD = 0x20002000


def build():
    inc = ["port_f10x", "inc", "Firmware_F10x/CMSIS", "Firmware_F10x/CMSIS/GD/GD32F10x/Include",
           "Firmware_F10x/GD32F10x_standard_peripheral/Include"]
    src_lib = "Firmware_F10x/GD32F10x_standard_peripheral/Source/"
    srcs = [str(RT / "ramtest.c"), str(ROOT / "port_f10x/system_aoci.c"),
            str(ROOT / src_lib / "gd32f10x_rcu.c"), str(ROOT / src_lib / "gd32f10x_adc.c"),
            str(ROOT / src_lib / "gd32f10x_misc.c")]
    cmd = ["arm-none-eabi-gcc", "-mcpu=cortex-m3", "-mthumb", "-mfloat-abi=soft", "-DGD32F10X_HD",
           "-DUSE_STDPERIPH_DRIVER", "-DHXTAL_VALUE=12000000U"]
    cmd += ["-I" + str(ROOT / i) for i in inc]
    cmd += ["-Os", "-std=gnu99", "-ffunction-sections", "-fdata-sections"] + srcs
    cmd += ["-nostartfiles", "-Wl,-T" + str(RT / "ramtest.ld"), "-Wl,--gc-sections",
            "-Wl,-Map=" + str(RT / "ramtest.map"), "-o", str(RT / "ramtest.elf")]
    subprocess.run(cmd, check=True)
    subprocess.run(["arm-none-eabi-objcopy", "-O", "binary", str(RT / "ramtest.elf"), str(RT / "ramtest.bin")], check=True)
    nm = subprocess.run(["arm-none-eabi-nm", str(RT / "ramtest.elf")], capture_output=True, text=True).stdout
    entry = [int(l.split()[0], 16) for l in nm.splitlines() if l.endswith(" test_start")][0]
    return entry


def main():
    entry = build()
    image = (RT / "ramtest.bin").read_bytes()
    print("image %d octets, entree 0x%08X" % (len(image), entry))
    assert entry == LOAD, "l'entree doit etre au debut de l'image"

    with ConnectHelper.session_with_chosen_probe(target_override="stm32f103rc", connect_mode="attach",
                                                 options={"frequency": 1000000}) as s:
        t = s.target
        bdtr = t.read32(0x40012C44)
        if (bdtr >> 15) & 1:
            print("ABANDON : TIM1 MOE = 1, les sorties de puissance sont actives. Rien n'est modifie.")
            return 2
        print("TIM1 BDTR = 0x%08X (MOE = 0), on continue" % bdtr)

        t.halt()
        t.write_memory_block8(RES, bytes(64 * 2))
        t.write_memory_block8(LOAD, list(image))
        t.write_core_register("sp", 0x2000B800)
        t.write_core_register("xpsr", 0x01000000)
        t.write_core_register("pc", entry | 1)
        t0 = time.time()
        t.resume()

        # attend le signal "mesure en cours" (magic = ...C0) : debut precis de la fenetre de 132 M cycles
        t_run0 = None
        deadline = time.time() + 20
        while time.time() < deadline:
            if t.read32(RES) == 0xA0C1A0C0:
                t_run0 = time.time()
                break
        deadline = time.time() + 20
        while time.time() < deadline:
            if t.is_halted():
                break
            time.sleep(0.01)
        t1 = time.time()
        if not t.is_halted():
            t.halt()
            print("ECHEC : l'essai ne s'est pas termine dans les 20 s")
        data = t.read_memory_block8(RES, 128)
        import struct
        (magic, step, sysclk, rcu_ctl, rcu_cfg0, cyc0, cyc1, dwt_ok) = struct.unpack_from("<8I", bytes(data), 0)
        adc = struct.unpack_from("<18H", bytes(data), 32)
        (adc_ok,) = struct.unpack_from("<I", bytes(data), 68)
        (acr,) = struct.unpack_from("<I", bytes(data), 92)
        idr = struct.unpack_from("<4I", bytes(data), 72)

    print("magic 0x%08X, etape %d, duree mur totale %.3f s" % (magic, step, t1 - t0))
    print("SystemCoreClock = %d Hz" % sysclk)
    print("RCU_CTL = 0x%08X, RCU_CFG0 = 0x%08X" % (rcu_ctl, rcu_cfg0))
    print("FLASH_ACR = 0x%08X (origine : 0x32)" % acr)
    cyc = (cyc1 - cyc0) & 0xFFFFFFFF
    print("DWT actif : %s ; cycles comptes : %d" % ("oui" if dwt_ok else "NON", cyc))
    if dwt_ok and cyc:
        # la mesure demarre un peu apres le resume (initialisations ~ 15 ms) : marge d'erreur annoncee
        est = cyc / (t1 - (t_run0 or t0))
        print("frequence estimee = %.3f MHz (fenetre mur %.3f s)" % (est / 1e6, t1 - (t_run0 or t0)))
    print("ADC (12 bits, voies 0..17) :", adc)
    for i, name in {0: "PA0", 1: "PA1", 2: "PA2", 3: "PA3", 4: "PA4 boutons", 5: "PA5", 6: "PA6 accelerateur",
                    7: "PA7 couple", 8: "PB0 temp moteur", 9: "PB1 temp controleur", 10: "PC0", 11: "PC1",
                    12: "PC2", 13: "PC3 tension batterie", 14: "PC4", 15: "PC5 alim driver", 16: "temp interne",
                    17: "Vrefint"}.items():
        print("  voie %2d %-20s %4d  (%.3f V)" % (i, name, adc[i], adc[i] * 3.3 / 4095))
    print("IDR  A=%04X  B=%04X  C=%04X  D=%04X" % tuple(i & 0xFFFF for i in idr))
    return 0


if __name__ == "__main__":
    rc = main()
    # remet la carte dans son etat d'origine (reset materiel, le firmware AOCI redemarre)
    with ConnectHelper.session_with_chosen_probe(target_override="stm32f103rc", connect_mode="attach",
                                                 options={"frequency": 1000000}) as s:
        s.target.reset()
    print("carte relancee (reset)")
    sys.exit(rc)
