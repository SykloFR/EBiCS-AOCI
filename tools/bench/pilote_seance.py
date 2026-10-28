"""Pilote de seance au home trainer : page locale (etapes, bips, metronome, home trainer FTMS) et capture CAN.
Sert tools/bench/pilote_seance.html sur http://127.0.0.1:8770 (Web Bluetooth exige localhost) et capture la telemetrie
de banc en ECOUTE SEULE (PCAN, meme decodage et meme CSV que bench_can_decode.py), avec les valeurs en direct pour la
page et un journal horodate des etapes (<nom>-journal.json a cote du CSV). Aucune trame n'est emise sur le bus.
Usage : python tools/bench/pilote_seance.py [--port 8770] [--dossier DOSSIER] [--canal PCAN_USBBUS1]"""
import argparse, collections, csv, json, os, re, sys, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bench_can_decode import PERIODE, decode   # noqa: E402

ICI = os.path.dirname(os.path.abspath(__file__))
PAGE = os.path.join(ICI, "pilote_seance.html")
DOSSIER_DEFAUT = os.path.normpath(os.path.join(ICI, "..", "..", "..", "aoci-firmware-re", "validation", "porte-4"))
DUREE_MAX = 900


class Capture:
    """Une capture a la fois, dans un fil a part ; la page lit l'etat et les dernieres valeurs par /api/etat."""

    def __init__(self, dossier, canal):
        self.dossier, self.canal = dossier, canal
        self.verrou = threading.Lock()
        self.fil = None
        self.arret = threading.Event()
        self.nom = self.erreur = None
        self.t0 = self.duree = 0.0
        self.n = 0
        self.dernier = {}
        self.t_trame = None
        self.journal = []

    def actif(self):
        return self.fil is not None and self.fil.is_alive()

    def demarrer(self, nom, duree):
        if self.actif():
            return "une capture tourne deja"
        nom = re.sub(r"[^A-Za-z0-9_.-]", "-", nom or "capture").strip("-.") or "capture"
        base, k = nom, 2
        while os.path.exists(os.path.join(self.dossier, nom + ".csv")):   # jamais d'ecrasement
            nom, k = f"{base}-{k}", k + 1
        self.nom, self.duree, self.erreur = nom, max(1.0, min(float(duree), DUREE_MAX)), None
        self.n, self.dernier, self.t_trame, self.journal = 0, {}, None, []
        self.arret.clear()
        self.t0 = time.time()
        self.fil = threading.Thread(target=self._boucle, daemon=True)
        self.fil.start()
        return None

    def evenement(self, texte):
        with self.verrou:
            e = {"t_pc": round(time.time() - self.t0, 3) if self.actif() else None,
                 "t_trame": self.t_trame, "heure": time.strftime("%H:%M:%S"), "texte": str(texte)[:300]}
            self.journal.append(e)
            if self.nom:
                self._ecrire_journal()
        return e

    def _ecrire_journal(self):
        with open(os.path.join(self.dossier, self.nom + "-journal.json"), "w", encoding="utf-8") as f:
            json.dump({"capture": self.nom + ".csv", "evenements": self.journal}, f, ensure_ascii=False, indent=1)

    def _boucle(self):
        import can
        stats = collections.defaultdict(lambda: {"n": 0, "trous": 0, "seq": None, "t0": None, "t1": None})
        chemin = os.path.join(self.dossier, self.nom + ".csv")
        try:
            with open(chemin, "w", newline="", encoding="utf-8") as fcsv, \
                    can.Bus(interface="pcan", channel=self.canal, bitrate=250000, state=can.BusState.PASSIVE) as bus:
                w = csv.writer(fcsv)
                w.writerow(["t", "trame", "seq", "champs"])
                while not self.arret.is_set() and time.time() - self.t0 < self.duree:
                    m = bus.recv(timeout=0.1)
                    if m is None or not m.is_extended_id or m.arbitration_id >> 16 != 0x1EE8 or len(m.data) < 1:
                        continue
                    cle = m.arbitration_id & 0xFFFF
                    s = stats[cle]
                    seq = m.data[0]
                    if s["seq"] is not None and seq != (s["seq"] + 1) & 0xFF:
                        s["trous"] += 1
                    s["seq"] = seq; s["n"] += 1
                    s["t0"] = s["t0"] if s["t0"] is not None else m.timestamp
                    s["t1"] = m.timestamp
                    champs = decode(cle, bytes(m.data))
                    w.writerow([f"{m.timestamp:.4f}", f"E{cle & 0xFFF:03X}", seq, champs])
                    with self.verrou:
                        self.dernier[cle] = champs
                        self.t_trame = round(m.timestamp, 4)
                        self.n += 1
        except Exception as e:   # bus absent, PCAN occupe... : la page l'affiche
            self.erreur = f"{type(e).__name__} : {e}"
        with open(os.path.join(self.dossier, self.nom + "-bilan.txt"), "w", encoding="utf-8") as f:
            f.write(f"capture {self.nom}.csv, ecoute seule {self.canal}, {time.time() - self.t0:.1f} s, {self.n} trames"
                    + (f", ERREUR {self.erreur}" if self.erreur else "") + "\n")
            for cle in sorted(PERIODE):
                s = stats.get(cle)
                if not s or s["n"] < 2:
                    f.write(f"  E{cle & 0xFFF:03X} : {'absente' if not s else 'une seule trame'}\n")
                    continue
                hz = (s["n"] - 1) / (s["t1"] - s["t0"]) if s["t1"] > s["t0"] else 0
                f.write(f"  E{cle & 0xFFF:03X} : {s['n']:5d} trames, {hz:6.1f} Hz (attendu {1000 / PERIODE[cle]:.0f}), "
                        f"trous {s['trous']}\n")
        with self.verrou:
            self.journal.append({"t_pc": round(time.time() - self.t0, 3), "t_trame": self.t_trame,
                                 "heure": time.strftime("%H:%M:%S"), "texte": "fin de capture"})
            self._ecrire_journal()

    def etat(self):
        with self.verrou:
            d = self.dernier
            g = lambda c, k: d.get(c, {}).get(k)
            live = dict(vitesse=g(0xE102, "vitesse"), cadence=g(0xE102, "cadence"), erps=g(0xE102, "erps"),
                        couple_mV=g(0xE102, "couple_mV"), iq_amont=g(0xE103, "iq_amont"), niveau=g(0xE103, "niveau"),
                        iq_cons=g(0xE000, "iq_cons"), iq=g(0xE000, "iq"), moe=g(0xE000, "moe"), defaut=g(0xE200, "defaut"),
                        vbat=g(0xE101, "vbat"), ibat=g(0xE101, "ibat"), regadc_hz=g(0xE212, "regadc_hz"),
                        zero_mV=g(0xE211, "zero_mV"), vmax=g(0xE20E, "vitesse_max_kmh"), git=g(0xE203, "git"))
            return dict(actif=self.actif(), nom=self.nom, ecoule=round(time.time() - self.t0, 1) if self.nom else None,
                        duree=self.duree, n=self.n, erreur=self.erreur, dossier=self.dossier, live=live)


class Gestion(BaseHTTPRequestHandler):
    cap = None

    def _json(self, obj, code=200):
        b = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def _corps(self):
        n = int(self.headers.get("Content-Length") or 0)
        try:
            return json.loads(self.rfile.read(n) or b"{}")
        except ValueError:
            return {}

    def do_GET(self):
        if self.path in ("/", "/index.html"):
            b = open(PAGE, "rb").read()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(b)))
            self.end_headers()
            self.wfile.write(b)
        elif self.path == "/api/etat":
            self._json(self.cap.etat())
        else:
            self._json({"erreur": "inconnu"}, 404)

    def do_POST(self):
        c = self._corps()
        if self.path == "/api/capture/demarrer":
            err = self.cap.demarrer(c.get("nom"), c.get("duree", 60))
            self._json({"ok": err is None, "erreur": err, "nom": self.cap.nom}, 200 if err is None else 409)
        elif self.path == "/api/capture/arreter":
            self.cap.arret.set()
            self._json({"ok": True})
        elif self.path == "/api/evenement":
            self._json(self.cap.evenement(c.get("texte", "")))
        else:
            self._json({"erreur": "inconnu"}, 404)

    def log_message(self, *a):   # console silencieuse
        pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8770)
    ap.add_argument("--dossier", default=DOSSIER_DEFAUT)
    ap.add_argument("--canal", default="PCAN_USBBUS1")
    a = ap.parse_args()
    os.makedirs(a.dossier, exist_ok=True)
    Gestion.cap = Capture(a.dossier, a.canal)
    srv = ThreadingHTTPServer(("127.0.0.1", a.port), Gestion)   # localhost seulement
    print(f"pilote de seance : http://localhost:{a.port}  (captures dans {a.dossier})", flush=True)
    srv.serve_forever()


if __name__ == "__main__":
    main()
