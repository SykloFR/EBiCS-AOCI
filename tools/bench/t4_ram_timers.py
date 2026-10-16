"""Test 4 : bases de temps TIMER1 (4 kHz) et TIMER2 (500 kHz) d'apres les formules de src/main.c, mesurees en RAM.
Usage : python tools/bench/t4_ram_timers.py [--repli]
"""
import struct, subprocess, sys, time
from pathlib import Path
from pyocd.core.helpers import ConnectHelper

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
RT = HERE / "ramtest"
RES, LOAD = 0x2000B000, 0x20002000
EXTRA = ["-DAOCI_TEST_NO_HXTAL"] if "--repli" in sys.argv else []


def build():
    inc = ["port_f10x", "inc", "Firmware_F10x/CMSIS", "Firmware_F10x/CMSIS/GD/GD32F10x/Include",
           "Firmware_F10x/GD32F10x_standard_peripheral/Include"]
    lib = "Firmware_F10x/GD32F10x_standard_peripheral/Source/"
    srcs = [str(RT / "ramtest_tim.c"), str(ROOT / "port_f10x/system_aoci.c")] + \
           [str(ROOT / lib / ("gd32f10x_%s.c" % m)) for m in ("rcu", "timer", "misc")]
    cmd = ["arm-none-eabi-gcc", "-mcpu=cortex-m3", "-mthumb", "-mfloat-abi=soft", "-DGD32F10X_HD",
           "-DUSE_STDPERIPH_DRIVER", "-DHXTAL_VALUE=12000000U"] + EXTRA
    cmd += ["-I" + str(ROOT / i) for i in inc]
    cmd += ["-Os", "-std=gnu99", "-ffunction-sections", "-fdata-sections"] + srcs
    cmd += ["-nostartfiles", "-Wl,-T" + str(RT / "ramtest.ld"), "-Wl,--gc-sections", "-o", str(RT / "ramtest_tim.elf")]
    subprocess.run(cmd, check=True)
    subprocess.run(["arm-none-eabi-objcopy", "-O", "binary", str(RT / "ramtest_tim.elf"), str(RT / "ramtest_tim.bin")], check=True)
    return (RT / "ramtest_tim.bin").read_bytes()


def main():
    image = build()
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
        deadline = time.time() + 10
        while time.time() < deadline and not t.is_halted():
            time.sleep(0.01)
        if not t.is_halted():
            t.halt(); print("ECHEC : pas termine")
        d = bytes(t.read_memory_block8(RES, 32))
    magic, sysclk, psc1, per1, psc2, upd, cnt2, cyc = struct.unpack("<8I", d)
    dur = cyc / float(sysclk)
    print("magic 0x%08X, SystemCoreClock %d Hz, fenetre %.4f s" % (magic, sysclk, dur))
    f1 = upd / dur
    f2 = cnt2 / dur
    print("TIMER1 : prescaler %d, periode %d -> %d mises a jour en %.4f s = %.1f Hz (attendu 4000)" % (psc1, per1, upd, dur, f1))
    print("TIMER2 : prescaler %d -> %d tops en %.4f s = %.1f kHz (attendu 500)" % (psc2, cnt2, dur, f2 / 1000.0))
    ok = abs(f1 - 4000) < 40 and abs(f2 - 500000) < 5000
    print("RESULTAT :", "OK" if ok else "ECART")
    return 0 if ok else 1


if __name__ == "__main__":
    rc = main()
    with ConnectHelper.session_with_chosen_probe(target_override="stm32f103rc", connect_mode="attach",
                                                 options={"frequency": 1000000}) as s:
        s.target.reset()
    print("carte relancee (reset)")
    sys.exit(rc)
