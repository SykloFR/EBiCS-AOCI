"""Commandes CAN de banc (T2, src/bench_can.c) : heartbeat, ESTOP, outil vecteur fixe. CANable en firmware PCAN, mode
actif (il émet). À n'utiliser qu'avec le build de banc, moteur débranché ou rotor libre, alim limitée.

  Banc(canal).demarrer_heartbeat()  heartbeat 20 Hz dans un fil dédié (time.perf_counter)
  .ol_set(angle_deg, ud) / .ol_stop() / .walk_start(ilim) / .walk_stop() / .estop()   -> statut E206 (0 accepté, 1 sans heartbeat, 2 défaut, 3 frein/assist,
                                                       4 invalide), None si pas de réponse en 0,5 s
  .dernier[cle]                     dernières trames de télémétrie décodées (bench_can_decode.decode)
  .fermer()                         WALK_STOP, OL_STOP puis arrêt du heartbeat ; appelé aussi en sortie de « with »

Usage direct : python tools/bench/bench_can_cmd.py estop | ol-stop"""
import os, sys, threading, time
import can
sys.path.insert(0, os.path.dirname(__file__))
from bench_can_decode import decode  # noqa: E402

ID_ESTOP, ID_HB, ID_CMD = 0x1DF0E000, 0x1DF0E001, 0x1DF0E002
OP_OL_SET, OP_OL_STOP = 0x10, 0x11
OP_WALK_START, OP_WALK_STOP = 0x20, 0x21
OP_PRECHARGE = 0x30
STATUTS = {0: "accepté", 1: "refusé : heartbeat absent ou < 500 ms", 2: "refusé : défaut verrouillé",
           3: "refusé : frein ou demande d'assistance", 4: "refusé : commande invalide",
           5: "refusé : offsets non calibrés (roue jamais arrêtée)",
           6: "refusé : angles du codeur non relevés ou état Hall invalide",
           7: "refusé : autre essai en cours (outil vecteur fixe ou walk)"}


class Banc:
    def __init__(self, canal="PCAN_USBBUS1"):
        self.bus = can.Bus(interface="pcan", channel=canal, bitrate=250000, state=can.BusState.ACTIVE)
        self.dernier, self.acks, self.cmd_seq = {}, [], 0
        self.capture = {}                 # E208 : rang -> (cap_seq vu en E209 a la reception, (ph1, ph2, ph3))
        self._verrou = threading.Lock()
        self._hb_actif = threading.Event()
        self._fin = threading.Event()
        threading.Thread(target=self._lire, daemon=True).start()
        threading.Thread(target=self._heartbeat, daemon=True).start()

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.fermer()

    def _envoyer(self, ident, data):
        with self._verrou:
            self.bus.send(can.Message(arbitration_id=ident, data=bytes(data), is_extended_id=True))

    def _lire(self):
        while not self._fin.is_set():
            m = self.bus.recv(0.05)
            if m is None or not m.is_extended_id or m.arbitration_id >> 16 != 0x1EE8:
                continue
            cle = m.arbitration_id & 0xFFFF
            if cle == 0xE206:
                self.acks.append((time.perf_counter(), m.data[1], m.data[2], m.data[3]))
            else:
                self.dernier[cle] = decode(cle, bytes(m.data))
                if cle == 0xE208:
                    d, sq = self.dernier[cle], self.dernier.get(0xE209, {}).get("cap_seq")
                    self.capture[d["rang"]], self.capture[d["rang"] + 1] = (sq, d["a"]), (sq, d["b"])

    def _heartbeat(self):
        n, t = 0, time.perf_counter()
        while not self._fin.is_set():
            t += 0.05
            if self._hb_actif.is_set():
                self._envoyer(ID_HB, (n & 0xFFFF).to_bytes(2, "little")); n += 1
            time.sleep(max(0.0, t - time.perf_counter()))

    def demarrer_heartbeat(self):
        self._hb_actif.set()

    def arreter_heartbeat(self):
        self._hb_actif.clear()

    def _commande(self, op, args=b""):
        self.cmd_seq = (self.cmd_seq + 1) & 0xFF
        seq, t0 = self.cmd_seq, time.perf_counter()
        self._envoyer(ID_CMD, bytes([op, seq]) + bytes(args))
        while time.perf_counter() - t0 < 0.5:
            for (t, o, s, statut) in list(self.acks):
                if o == op and s == seq and t >= t0:
                    return statut
            time.sleep(0.005)
        return None

    def ol_set(self, angle_deg, ud):
        a = int(round(angle_deg % 360 * 65536 / 360)) & 0xFFFF
        return self._commande(OP_OL_SET, a.to_bytes(2, "little") + bytes([max(0, min(255, int(ud)))]))

    def ol_stop(self):
        return self._commande(OP_OL_STOP)

    def precharge(self, ms):
        return self._commande(OP_PRECHARGE, bytes([max(0, min(255, int(ms)))]))

    def walk_start(self, ilim_pts):
        return self._commande(OP_WALK_START, bytes([max(0, min(255, int(ilim_pts)))]))

    def walk_stop(self):
        return self._commande(OP_WALK_STOP)

    def estop(self):
        self._envoyer(ID_ESTOP, b"\x00")

    def fermer(self):
        try:
            self.walk_stop()
            self.ol_stop()
        finally:
            self.arreter_heartbeat()
            self._fin.set()
            time.sleep(0.1)
            self.bus.shutdown()


if __name__ == "__main__":
    with Banc() as b:
        if sys.argv[1:] == ["estop"]:
            b.estop(); time.sleep(0.2); print("ESTOP envoyé")
        elif sys.argv[1:] == ["ol-stop"]:
            print("OL_STOP :", STATUTS.get(b.ol_stop(), "pas de réponse"))
        else:
            sys.exit(__doc__)
