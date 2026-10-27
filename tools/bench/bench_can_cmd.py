"""Commandes CAN de banc (T2, src/bench_can.c) : heartbeat, ESTOP, outil vecteur fixe. CANable en firmware PCAN, mode
actif (il émet). À n'utiliser qu'avec le build de banc, moteur débranché ou rotor libre, alim limitée.

  Banc(canal).demarrer_heartbeat()  heartbeat 20 Hz dans un fil dédié (time.perf_counter)
  .ol_set(angle_deg, ud) / .ol_stop() / .walk_start(ilim) / .walk_stop() / .estop()   -> statut E206 (0 accepté, 1 sans heartbeat, 2 défaut, 3 frein/assist,
                                                       4 invalide), None si pas de réponse en 0,5 s
  .dernier[cle]                     dernières trames de télémétrie décodées (bench_can_decode.decode), d'âge inconnu :
                                    un verdict ne se prend que sur une trame fraîche (ci-dessous)
  .recu[cle] / .age(cle)            heure de réception (time.monotonic) / âge en s de la dernière trame cle (None : jamais)
  .frais(cle, depuis=t)             dernière trame cle si fraîche (âge < 2 périodes) et reçue après t, sinon None
  .attendre_frais(cle, apres=t, timeout)   attend une trame cle reçue après t ; None au timeout (verdict indéterminé)
  .etat(cle, champ)                 « frais », « moyen » (filtré côté firmware, voir MOYEN), « retard » (2 à 5 périodes),
                                    « fige » (> 5 périodes, ou trame répétée à l'identique, compteur d[0] immobile), « absent »
  .t_moe0 / .moe_rx[cle]            réception du front MOE 1 -> 0 (E000) / MOE connu à la réception de la trame cle
  .defaut_apres(t, timeout)         défaut lu dans une E200 reçue après t : int, ou None (indéterminé, jamais « 0 »)
  .fermer()                         WALK_STOP, OL_STOP puis arrêt du heartbeat ; appelé aussi en sortie de « with »

Usage direct : python tools/bench/bench_can_cmd.py estop | ol-stop"""
import os, sys, threading, time
import can
sys.path.insert(0, os.path.dirname(__file__))
from bench_can_decode import decode, PERIODE  # noqa: E402

ID_ESTOP, ID_HB, ID_CMD = 0x1DF0E000, 0x1DF0E001, 0x1DF0E002
OP_OL_SET, OP_OL_STOP = 0x10, 0x11
OP_WALK_START, OP_WALK_STOP = 0x20, 0x21
OP_PRECHARGE = 0x30
STATUTS = {0: "accepté", 1: "refusé : heartbeat absent ou < 500 ms", 2: "refusé : défaut verrouillé",
           3: "refusé : frein ou demande d'assistance", 4: "refusé : commande invalide",
           5: "refusé : offsets non calibrés (roue jamais arrêtée)",
           6: "refusé : angles du codeur non relevés ou état Hall invalide",
           7: "refusé : autre essai en cours (outil vecteur fixe ou walk)"}
FLUX = (0xE000, 0xE001, 0xE101, 0xE102)   # trames gardees en serie par b.flux (i_q, u_q, u_abs, Ibat, erps)
FRAIS_N, FIGE_N = 2, 5            # periodes nominales (PERIODE, ms, table trames[] de src/bench_can.c)
# grandeurs filtrees ou moyennees cote firmware : une valeur fraiche reste en retard sur l'instant (src/main.c, src/FOC.c)
MOYEN = {(0xE000, "iq"): "IIR 1/8 a la PWM (FOC_calculation ; brut avec l'outil vecteur fixe)",
         (0xE000, "id"): "IIR 1/8 a la PWM (FOC_calculation ; brut avec l'outil vecteur fixe)",
         (0xE100, "ph1_eff"): "efficace sur la fenetre de la trame (50 ms, 4 kHz)",
         (0xE100, "ph2_eff"): "efficace sur la fenetre de la trame (50 ms, 4 kHz)",
         (0xE100, "ph3_eff"): "efficace sur la fenetre de la trame (50 ms, 4 kHz)",
         (0xE100, "crete"): "maximum sur la fenetre de la trame (50 ms)",
         (0xE101, "vbat"): "IIR 1/64 a 4 kHz (voltage_raw_cumulated, reg_ADC_processing)",
         (0xE101, "vbat_brut"): "IIR 1/64 a 4 kHz (voltage_raw_filtered)",
         (0xE101, "ibat"): "IIR 1/64 a 4 kHz (battery_current_cumulated)",
         (0xE101, "soc"): "calcule sur Vbat filtree",
         (0xE102, "erps"): "IIR 1/32 par front Hall (TIMER2_IRQHandler) : fige sans front, ne retombe pas a 0",
         (0xE102, "vitesse"): "IIR par impulsion roue (Speed_processing), mise a 0 apres 5 s sans impulsion",
         (0xE103, "couple_filtre"): "IIR 1/2^TQfilter par impulsion PAS"}


class Banc:
    def __init__(self, canal="PCAN_USBBUS1", bus=None):
        # bus : autre bus python-can (essai hors ligne : can.Bus(interface="virtual")) ; par defaut le CANable PCAN
        self.bus = bus or can.Bus(interface="pcan", channel=canal, bitrate=250000, state=can.BusState.ACTIVE)
        self.dernier, self.acks, self.cmd_seq = {}, [], 0
        self.recu, self.repete, self._brut = {}, {}, {}   # cle -> time.monotonic a la reception / trame identique a la precedente
        self.moe_rx = {}                  # cle -> MOE (E000) connu a la reception : 0 = trame d'apres l'arret du pont
        self.t_moe0 = None                # time.monotonic de la premiere E000 a MOE 0 apres une E000 a MOE 1 (None : pont actif)
        self.capture = {}                 # E208 : rang -> (cap_seq vu en E209 a la reception, (ph1, ph2, ph3))
        self.fronts = {}                  # E20A : (hj_seq, rang) -> trame decodee + t_rx (journal des fronts Hall)
        self.avant_trip = {}              # E20B : (pt_seq, rang) -> trame decodee (capture des periodes avant le trip)
        self.flux = None                  # liste : (time.monotonic, cle, trame decodee) des trames de FLUX, si active (= [])
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
            t = time.monotonic()
            if cle == 0xE206:
                self.acks.append((time.perf_counter(), m.data[1], m.data[2], m.data[3]))
            else:
                self.repete[cle] = self._brut.get(cle) == bytes(m.data)   # d[0] = sequence : jamais deux fois la meme
                self._brut[cle] = bytes(m.data)
                self.dernier[cle] = decode(cle, bytes(m.data))
                if cle == 0xE000:
                    moe = self.dernier[cle]["moe"]
                    if not moe and self.moe_rx.get(0xE000) and self.t_moe0 is None:
                        self.t_moe0 = t
                    elif moe:
                        self.t_moe0 = None
                self.moe_rx[cle] = self.dernier.get(0xE000, {}).get("moe")
                self.recu[cle] = t
                if self.flux is not None and cle in FLUX:
                    self.flux.append((t, cle, self.dernier[cle]))
                if cle == 0xE20A and self.dernier[cle]["rang"] != 0xFF:
                    d = self.dernier[cle]
                    self.fronts[(d["hj_seq"], d["rang"])] = dict(d, t_rx=t)   # t_rx : time.monotonic a la reception
                if cle == 0xE20B and self.dernier[cle]["rang"] != 0xFF and self.dernier[cle]["figee"]:
                    d = self.dernier[cle]
                    self.avant_trip[(d["pt_seq"], d["rang"])] = d
                if cle == 0xE208:
                    d, sq = self.dernier[cle], self.dernier.get(0xE209, {}).get("cap_seq")
                    self.capture[d["rang"]], self.capture[d["rang"] + 1] = (sq, d["a"]), (sq, d["b"])

    def age(self, cle):
        t = self.recu.get(cle)
        return None if t is None else time.monotonic() - t

    def frais(self, cle, depuis=None):
        """derniere trame cle si recue apres depuis (time.monotonic) et d'age < FRAIS_N periodes, sinon None"""
        a, t = self.age(cle), self.recu.get(cle)
        if a is None or a >= FRAIS_N * PERIODE.get(cle, 1000) / 1000 or (depuis is not None and t <= depuis):
            return None
        return self.dernier[cle]

    def attendre_frais(self, cle, apres, timeout=None):
        """attend une trame cle recue apres l'instant apres (time.monotonic) ; None au timeout (defaut FIGE_N periodes)"""
        t_lim = time.monotonic() + (FIGE_N * PERIODE.get(cle, 1000) / 1000 if timeout is None else timeout)
        while True:
            t = self.recu.get(cle)
            if t is not None and t > apres:
                return self.dernier[cle]
            if time.monotonic() >= t_lim:
                return None
            time.sleep(0.005)

    def etat(self, cle, champ=None):
        a, p = self.age(cle), PERIODE.get(cle, 1000) / 1000
        if a is None:
            return "absent"
        if a > FIGE_N * p or self.repete.get(cle):
            return "fige"
        if a >= FRAIS_N * p:
            return "retard"
        return "moyen" if (cle, champ) in MOYEN else "frais"

    def defaut_apres(self, t, timeout=0.5):
        """defaut lu dans une E200 recue apres t (MOE retombe, walk_stop...) : int, ou None = indetermine"""
        d = self.attendre_frais(0xE200, apres=t, timeout=timeout)
        return None if d is None else d["defaut"]

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
