"""Télémétrie CAN de banc (src/bench_can.c) : décodage, journal et contrôle des séquences, en ÉCOUTE SEULE (PCAN).
Trames 0x1EE8xxxx (contrôleur en mode banc 0x1E -> PC 0x1D). Affiche un état chaque seconde, écrit un CSV décodé
(une ligne par trame) et termine par un bilan : débit par trame, trous de séquence, trames perdues annoncées.
Sans aucun autre noeud sur le bus (afficheur debranche), personne n'acquitte : le controleur reemet en boucle et compte
ses trames perdues. --actif met alors le CANable en mode actif : il acquitte, mais n'emet toujours aucune trame.
Usage : python tools/bench/bench_can_decode.py [duree_s=20] [--csv FICHIER] [--canal PCAN_USBBUS1] [--actif]"""
import argparse, collections, csv, struct, sys, time
import can

PERIODE = {0xE000: 20, 0xE001: 20, 0xE002: 50, 0xE100: 50, 0xE101: 50, 0xE102: 50, 0xE103: 50,
           0xE200: 100, 0xE201: 100, 0xE202: 100, 0xE203: 1000, 0xE204: 1000, 0xE205: 1000, 0xE207: 1000,
           0xE208: 20, 0xE209: 1000}


def decode(cle, d):
    u8 = lambda i: d[i]
    u16 = lambda i: struct.unpack_from("<H", d, i)[0]
    i16 = lambda i: struct.unpack_from("<h", d, i)[0]
    if cle == 0xE000:
        f = d[7]
        return dict(iq_cons=i16(1), iq=i16(3), id=i16(5), moe=f & 1, pwm_on=f >> 1 & 1, walk=f >> 2 & 1, frein=f >> 3 & 1,
                    hb=f >> 4 & 1, verrou=f >> 5 & 1, outil=f >> 6 & 1, offsets_ok=f >> 7 & 1)
    if cle == 0xE001:
        return dict(ud=i16(1), uq=i16(3), uabs=u16(5), hall_gpio=d[7] & 7, hall_etat=d[7] >> 3 & 7)
    if cle == 0xE002:
        return dict(outil=u8(1), ol_ud=i16(2), ol_angle_deg=round(u16(4) * 360 / 65536, 1), keepalive=u16(6))
    if cle == 0xE100:
        return dict(ph1_eff=u16(1) / 10, ph2_eff=u16(3) / 10, ph3_eff=u16(5) / 10, crete=u8(7))
    if cle == 0xE101:
        return dict(vbat=u16(1) / 100, ibat=i16(3) / 100, vbat_brut=u16(5), soc=u8(7))
    if cle == 0xE102:
        return dict(vitesse=u16(1) / 100, erps=u16(3), cadence=u8(5), couple_mV=u16(6))
    if cle == 0xE103:
        return dict(couple_filtre=u16(1), niveau=u8(3), walk=u8(4), iq_amont=i16(5), walk_ilim=u8(7))
    if cle == 0xE200:
        return dict(defaut=u8(1), defaut_banc=u8(2), moe=d[3] & 1, pwm_on=d[3] >> 1 & 1, outil=d[3] >> 2 & 1,
                    hall_brut=u8(4), half_rot=u16(5), cause_reset=hex(u8(7)))
    if cle == 0xE201:
        t_mot = i16(3)
        return dict(t_ctrl=i16(1), t_moteur=None if t_mot == 0x7FFF else t_mot, adc_pb1=u16(5), pile=u8(7))
    if cle == 0xE202:
        return dict(perdues=u16(1), rx_perdues=u8(3), tec=u8(4), rec=u8(5), cycles_max=u16(6))
    if cle == 0xE203:
        b = d[7]
        return dict(version=u16(1), git=f"{struct.unpack_from('<I', d, 3)[0]:08x}", no_eeprom=b & 1, temp_req=b >> 1 & 1,
                    signe_neg=b >> 2 & 1, git_modifie=b >> 3 & 1)
    if cle == 0xE204:
        return dict(ph_max=u8(1), ph_trip=u8(2), ilim=u8(3), u_max=u8(4) * 8, v_min=u16(5), ol_ud_max=u8(7))
    if cle == 0xE205:
        return dict(off_ph1=u16(1), off_ph2=u16(3), off_ph3=u16(5), off_dans_plage=d[7] & 1, pont_appliques=d[7] >> 1 & 1,
                    calibration_faite=d[7] >> 2 & 1, roue_arretee=d[7] >> 3 & 1, hall_valide=d[7] >> 4 & 1)
    if cle == 0xE208:
        i8 = lambda i: struct.unpack_from("<b", d, i)[0]
        return dict(rang=u8(1), a=(i8(2), i8(3), i8(4)), b=(i8(5), i8(6), i8(7)))
    if cle == 0xE209:
        return dict(cap_n=u8(1), cap_trip=u8(2), precharge_ms=u8(3), cap_seq=u8(4), chctl2=hex(u16(5)), cap_N=u8(7))
    if cle == 0xE207:
        return dict(variante=u8(1), pont_ph1=u16(2), pont_ph2=u16(4), pont_ph3=u16(6))
    return {}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("duree", nargs="?", type=float, default=20.0)
    ap.add_argument("--csv")
    ap.add_argument("--canal", default="PCAN_USBBUS1")
    ap.add_argument("--actif", action="store_true", help="acquitter les trames (aucune emission)")
    a = ap.parse_args()
    stats = collections.defaultdict(lambda: {"n": 0, "trous": 0, "seq": None, "t0": None, "t1": None})
    dernier = {}
    w = None
    if a.csv:
        fcsv = open(a.csv, "w", newline="", encoding="utf-8")
        w = csv.writer(fcsv)
        w.writerow(["t", "trame", "seq", "champs"])
    with can.Bus(interface="pcan", channel=a.canal, bitrate=250000, state=can.BusState.ACTIVE if a.actif else can.BusState.PASSIVE) as bus:
        print(f"{'acquittement sans emission' if a.actif else 'ecoute seule'} {a.canal}, {a.duree:.0f} s", flush=True)
        t0 = t_aff = time.time()
        while time.time() - t0 < a.duree:
            m = bus.recv(timeout=0.1)
            if m is not None and m.is_extended_id and m.arbitration_id >> 16 == 0x1EE8 and len(m.data) >= 1:
                cle = m.arbitration_id & 0xFFFF
                s = stats[cle]
                seq = m.data[0]
                if s["seq"] is not None and seq != (s["seq"] + 1) & 0xFF:
                    s["trous"] += 1
                s["seq"] = seq; s["n"] += 1
                s["t0"] = s["t0"] if s["t0"] is not None else m.timestamp
                s["t1"] = m.timestamp
                champs = decode(cle, bytes(m.data))
                dernier[cle] = champs
                if w:
                    w.writerow([f"{m.timestamp:.4f}", f"E{cle & 0xFFF:03X}", seq, champs])
            if time.time() - t_aff >= 1.0 and dernier:
                t_aff = time.time()
                e0, e101, e200 = dernier.get(0xE000, {}), dernier.get(0xE101, {}), dernier.get(0xE200, {})
                print(f"{time.time() - t0:5.0f} s  MOE {e0.get('moe')} défaut {e200.get('defaut')}  iq {e0.get('iq')} id {e0.get('id')}  "
                      f"Vbat {e101.get('vbat')} Ibat {e101.get('ibat')}  Hall {e200.get('hall_brut')}  "
                      f"ph eff {dernier.get(0xE100, {}).get('ph1_eff')}/{dernier.get(0xE100, {}).get('ph2_eff')}/"
                      f"{dernier.get(0xE100, {}).get('ph3_eff')}", flush=True)
    if w:
        fcsv.close()
    print("\nbilan par trame (débit mesuré / attendu, trous de séquence) :")
    for cle in sorted(PERIODE):
        s = stats.get(cle)
        if not s or s["n"] < 2:
            print(f"  E{cle & 0xFFF:03X} : {'absente' if not s else 'une seule trame'}")
            continue
        hz = (s["n"] - 1) / (s["t1"] - s["t0"]) if s["t1"] > s["t0"] else 0
        print(f"  E{cle & 0xFFF:03X} : {s['n']:5d} trames, {hz:6.1f} Hz (attendu {1000 / PERIODE[cle]:.0f}), trous {s['trous']}")
    print("\ndernières valeurs :")
    for cle in sorted(dernier):
        print(f"  E{cle & 0xFFF:03X} {dernier[cle]}")


if __name__ == "__main__":
    main()
