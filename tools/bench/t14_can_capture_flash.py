"""Capture d'un flash officiel AOCI par le CAN, en ECOUTE SEULE (aucune trame emise, aucun acquittement).
CANable en firmware PCAN (vu comme PEAK PCAN-USB), 250 kbit/s, trames d'erreur incluses. Chaque trame est ecrite et
vidangee aussitot (rien de perdu si le PC ou le bus decroche), avec un compteur en direct chaque seconde.
A lancer AVANT la mise sous tension du controleur : le bootloader n'ecoute que ~0,5 s apres la mise sous tension.
Sortie : JOURNAL (une ligne par trame : t_s, id, src, dst, op, index, sub, dlc, octets) puis bilan par identifiant.
Usage : python tools/bench/t14_can_capture_flash.py JOURNAL.txt [duree_s=300] [canal=PCAN_USBBUS1]"""
import collections, sys, time
import can
from can.interfaces.pcan.basic import PCAN_ALLOW_ERROR_FRAMES, PCAN_PARAMETER_ON

JOURNAL = sys.argv[1]
DUREE = float(sys.argv[2]) if len(sys.argv) > 2 else 300.0
CANAL = sys.argv[3] if len(sys.argv) > 3 else "PCAN_USBBUS1"


def champs(i):
    return f"{i >> 24 & 0x1F:#04x} {i >> 19 & 0x1F:#04x} {i >> 16 & 7} {i >> 8 & 0xFF:#04x} {i & 0xFF:#04x}"


ids, n, n_err, t_prem = collections.OrderedDict(), 0, 0, None
with can.Bus(interface="pcan", channel=CANAL, bitrate=250000, state=can.BusState.PASSIVE) as bus, \
        open(JOURNAL, "w", encoding="utf-8") as j:
    canal = bus.channel_info
    bus.m_objPCANBasic.SetValue(bus.m_PcanHandle, PCAN_ALLOW_ERROR_FRAMES, PCAN_PARAMETER_ON)
    j.write(f"# capture flash AOCI, {canal}, 250 kbit/s, ecoute seule, debut {time.strftime('%Y-%m-%d %H:%M:%S')}\n")
    j.write("# t_s id src dst op index sub dlc octets\n")
    print(f"ecoute seule {canal}, {DUREE:.0f} s -> {JOURNAL}. Mettre le controleur sous tension maintenant.", flush=True)
    t0 = t_aff = time.time()
    n_aff = 0
    while time.time() - t0 < DUREE:
        m = bus.recv(timeout=0.1)
        if m is not None:
            if t_prem is None:
                t_prem = m.timestamp
            t = m.timestamp - t_prem
            if m.is_error_frame:
                n_err += 1
                j.write(f"{t:10.4f} ERREUR\n")
            else:
                n += 1
                d = m.data.hex(" ")
                if m.is_extended_id:
                    j.write(f"{t:10.4f} {m.arbitration_id:#010x} {champs(m.arbitration_id)} {m.dlc} {d}\n")
                else:
                    j.write(f"{t:10.4f} {m.arbitration_id:#05x} std - - - - {m.dlc} {d}\n")
                v = ids.setdefault(m.arbitration_id, [0, t, t, d])
                v[0] += 1; v[2] = t; v[3] = d
            j.flush()
        if time.time() - t_aff >= 1.0:
            print(f"{time.time() - t0:6.0f} s : {n} trames (+{n - n_aff}), {n_err} erreurs, {len(ids)} identifiants", flush=True)
            t_aff, n_aff = time.time(), n
print(f"\n{n} trames, {n_err} trames d'erreur, {len(ids)} identifiants")
for i, (c, t1, t2, d) in ids.items():
    print(f"{i:#010x} {champs(i)}  x{c:6d}  de {t1:9.3f} a {t2:9.3f} s  derniere : {d}")
