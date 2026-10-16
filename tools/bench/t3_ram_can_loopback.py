"""Test 3 : CAN en boucle interne silencieuse (aucune trame sur le bus), debit mesure par le compteur de cycles.
Usage : python tools/bench/t3_ram_can_loopback.py
"""
import struct, subprocess, sys, time
from pathlib import Path
from pyocd.core.helpers import ConnectHelper

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
RT = HERE / "ramtest"
RES, LOAD = 0x2000B000, 0x20002000
# ex. : python t3_ram_can_loopback.py --repli   force l'echec du quartz (AOCI_TEST_NO_HXTAL) pour essayer le repli HSI 64 MHz
EXTRA_DEFS = ["-DAOCI_TEST_NO_HXTAL"] if "--repli" in sys.argv else []
EXPECT = 64000000 if "--repli" in sys.argv else 66000000


def build():
    inc = ["port_f10x", "inc", "Firmware_F10x/CMSIS", "Firmware_F10x/CMSIS/GD/GD32F10x/Include",
           "Firmware_F10x/GD32F10x_standard_peripheral/Include"]
    lib = "Firmware_F10x/GD32F10x_standard_peripheral/Source/"
    srcs = [str(RT / "ramtest_can.c"), str(ROOT / "port_f10x/system_aoci.c")] + \
           [str(ROOT / lib / ("gd32f10x_%s.c" % m)) for m in ("rcu", "can", "misc")]
    cmd = ["arm-none-eabi-gcc", "-mcpu=cortex-m3", "-mthumb", "-mfloat-abi=soft", "-DGD32F10X_HD",
           "-DUSE_STDPERIPH_DRIVER", "-DARM_MATH_CM3", "-DHXTAL_VALUE=12000000U"] + EXTRA_DEFS
    cmd += ["-I" + str(ROOT / i) for i in inc]
    cmd += ["-Os", "-std=gnu99", "-ffunction-sections", "-fdata-sections"] + srcs
    cmd += ["-nostartfiles", "-Wl,-T" + str(RT / "ramtest.ld"), "-Wl,--gc-sections",
            "-o", str(RT / "ramtest_can.elf")]
    subprocess.run(cmd, check=True)
    subprocess.run(["arm-none-eabi-objcopy", "-O", "binary", str(RT / "ramtest_can.elf"), str(RT / "ramtest_can.bin")], check=True)
    return (RT / "ramtest_can.bin").read_bytes()


def can_crc15(bits):
    crc = 0
    for b in bits:
        nxt = b ^ ((crc >> 14) & 1)
        crc = (crc << 1) & 0x7FFF
        if nxt:
            crc ^= 0x4599
    return [(crc >> i) & 1 for i in range(14, -1, -1)]


def frame_bits(ext_id, data):
    """Nombre de bits d'une trame etendue de donnees, de SOF a EOF + IFS (bourrage inclus)."""
    bits = [0]
    bits += [(ext_id >> i) & 1 for i in range(28, 17, -1)]      # ID[28:18]
    bits += [1, 1]                                              # SRR, IDE
    bits += [(ext_id >> i) & 1 for i in range(17, -1, -1)]      # ID[17:0]
    bits += [0, 0, 0]                                           # RTR, r1, r0
    bits += [(len(data) >> i) & 1 for i in range(3, -1, -1)]    # DLC
    for byte in data:
        bits += [(byte >> i) & 1 for i in range(7, -1, -1)]
    bits += can_crc15(bits)
    out, run, last = [], 0, None
    for b in bits:
        out.append(b)
        run = run + 1 if b == last else 1
        last = b
        if run == 5:
            out.append(1 - b)
            last, run = 1 - b, 1
    return len(out) + 1 + 2 + 7 + 3                              # CRC delimiter, ACK (2), EOF (7), IFS (3)


def main():
    image = build()
    with ConnectHelper.session_with_chosen_probe(target_override="stm32f103rc", connect_mode="attach",
                                                 options={"frequency": 1000000}) as s:
        t = s.target
        if (t.read32(0x40012C44) >> 15) & 1:
            print("ABANDON : TIM1 MOE = 1")
            return 2
        t.halt()
        t.write_memory_block8(RES, bytes(160))
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
        d = bytes(t.read_memory_block8(RES, 100))
    magic, step, sysclk = struct.unpack_from("<3I", d, 0)
    t_rx = struct.unpack_from("<2I", d, 12)
    id_rx = struct.unpack_from("<2I", d, 20)
    data_rx = [d[28:36], d[36:44]]
    len_rx = struct.unpack_from("<2I", d, 44)
    ff_rx = struct.unpack_from("<2I", d, 52)
    err, presc, bs1, bs2 = struct.unpack_from("<4I", d, 60) if False else (struct.unpack_from("<I", d, 60)[0], *struct.unpack_from("<3I", d, 64))
    print("magic 0x%08X etape %d SystemCoreClock %d (attendu %d)" % (magic, step, sysclk, EXPECT))
    print("prescaler CAN utilise : %d" % presc)
    ok = True
    for i in range(2):
        exp_id = 0x12F83200 + i
        exp_data = bytes((0x10 * (i + 1) + k * 3 + 0x07) & 0xFF for k in range(8))
        good = (id_rx[i] == exp_id and bytes(data_rx[i]) == exp_data and len_rx[i] == 8)
        ok &= good
        print("trame %d : ID recu 0x%08X (attendu 0x%08X) dlc %d donnees %s -> %s" %
              (i + 1, id_rx[i], exp_id, len_rx[i], bytes(data_rx[i]).hex(), "OK" if good else "ECART"))
    delta = (t_rx[1] - t_rx[0]) & 0xFFFFFFFF
    nbits = frame_bits(0x12F83201, bytes((0x10 * 2 + k * 3 + 0x07) & 0xFF for k in range(8)))
    baud = nbits / (delta / float(sysclk))
    print("cycles entre les deux receptions : %d ; bits de la 2e trame (bourrage compris) : %d" % (delta, nbits))
    print("debit mesure = %.1f kbit/s (attendu 250)" % (baud / 1000.0))
    print("registre d'erreur CAN_ERR = 0x%08X" % err)
    return 0 if ok else 1


if __name__ == "__main__":
    rc = main()
    with ConnectHelper.session_with_chosen_probe(target_override="stm32f103rc", connect_mode="attach",
                                                 options={"frequency": 1000000}) as s:
        s.target.reset()
    print("carte relancee (reset)")
    sys.exit(rc)
