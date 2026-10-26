"""Capture CAN en ECOUTE SEULE (aucune trame emise, aucun acquittement) via un adaptateur vu comme PEAK PCAN-USB.
250 kbit/s (debit AOCI, analyse-can.md). Identifiants AOCI 29 bits : Src[28..24] Dst[23..19] Op[18..16] Index[15..8] Sub[7..0].
Ecrit chaque trame dans un journal texte, puis un bilan par identifiant (nombre, periode moyenne, derniere charge utile).
Prerequis : pilote PEAK installe (PCANBasic.dll), python-can (pip install --user python-can).
Usage : python tools/bench/t12_can_ecoute.py JOURNAL.txt [duree_s=30] [canal=PCAN_USBBUS1]"""
import collections, sys, time
import can

JOURNAL = sys.argv[1]
DUREE = float(sys.argv[2]) if len(sys.argv) > 2 else 30.0
CANAL = sys.argv[3] if len(sys.argv) > 3 else "PCAN_USBBUS1"


def decode(m):
    if not m.is_extended_id:
        return f"std {m.arbitration_id:#05x}"
    i = m.arbitration_id
    return (f"src {i >> 24 & 0x1F:#04x} dst {i >> 19 & 0x1F:#04x} op {i >> 16 & 7} "
            f"index {i >> 8 & 0xFF:#04x} sub {i & 0xFF:#04x}")


vus = collections.OrderedDict()
with can.Bus(interface="pcan", channel=CANAL, bitrate=250000, state=can.BusState.PASSIVE) as bus, \
        open(JOURNAL, "w", encoding="utf-8") as j:
    print(f"ecoute seule {CANAL} 250 kbit/s pendant {DUREE:.0f} s -> {JOURNAL}")
    t0 = time.time()
    while time.time() - t0 < DUREE:
        m = bus.recv(timeout=0.5)
        if m is None:
            continue
        if m.is_error_frame:
            j.write(f"{m.timestamp:.4f} TRAME D'ERREUR\n")
            continue
        donnees = m.data.hex(" ")
        j.write(f"{m.timestamp:.4f} {m.arbitration_id:#010x} {decode(m)} dlc {m.dlc} {donnees}\n")
        v = vus.setdefault(m.arbitration_id, {"n": 0, "t": [], "d": "", "m": m})
        v["n"] += 1
        v["t"].append(m.timestamp)
        v["d"] = donnees
print(f"{sum(v['n'] for v in vus.values())} trames, {len(vus)} identifiants")
for i, v in sorted(vus.items()):
    per = (v["t"][-1] - v["t"][0]) / (v["n"] - 1) * 1000 if v["n"] > 1 else 0
    print(f"{i:#010x} {decode(v['m']):45s} x{v['n']:5d} periode {per:7.1f} ms  derniere : {v['d']}")
