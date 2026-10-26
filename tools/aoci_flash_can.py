"""Flash de l'application par le CAN, via le bootloader AOCI d'origine (jamais modifié, 0x08000000-0x08004FFF).

Rejoue la séquence du programmateur AOCI « Intelligent Offline Programmer » capturée le 26/10/2026
(aoci-firmware-re/bootloader/README.md, « Flash officiel capturé ») :
  1. en boucle toutes les 20 ms : 0x15FFC005 [00] puis 0x1591B000 [5C A0 02 00], jusqu'à l'accord 0x12AAB000
     (le bootloader n'écoute qu'environ 0,5 s après la MISE SOUS TENSION du contrôleur) ;
  2. taille 0x1594B001 [3 octets gros-boutistes] -> accusé 0x12AAB001 ;
  3. blocs de 8 octets 0x1595nnnn (n sur 16 bits) -> accusé 0x12AAnnnn pour chacun ;
  4. dernier bloc 0x1596nnnn (le reste, ici les 4 octets du CRC) -> accusé 0x12AAnnnn (vérification du CRC).
Nœuds : outil 0x15 (comme le programmateur), contrôleur 0x12. 250 kbit/s, IDs 29 bits.

Image acceptée : image complète du build (*_aoci_full.bin, 0x36000 octets) ou fichier AOCI (en-tête 16 octets
5C A0 82 00 … CRKXY2). On envoie 0x35FC4 octets : zone appli 0x08005000-0x0803AFBF + CRC32 STM32 à 0x0803AFC0, vérifié
avant tout envoi. Aucune relance d'un bloc non acquitté (une réécriture de flash non effacée échouerait) : en cas
d'échec, le CRC reste faux, le bootloader garde la main à la mise sous tension suivante, on relance l'outil. Le SWD reste
le secours.

Usage :
  python tools/aoci_flash_can.py IMAGE                  # vérifie l'image, n'émet rien
  python tools/aoci_flash_can.py IMAGE --simuler        # séquence complète contre un bootloader simulé
  python tools/aoci_flash_can.py IMAGE --go [--journal FICHIER] [--attente 120] [--canal PCAN_USBBUS1]
         (CANable en firmware PCAN, mode actif : il émet et acquitte ; mettre le contrôleur sous tension après le lancement)
"""
import argparse, hashlib, struct, sys, time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from make_aoci_image import APP_LEN, check_vectors, stm32_crc  # noqa: E402

TAILLE = APP_LEN + 4                       # 0x35FC4 : image + CRC
ANNONCE, DEMANDE, ACCORD = 0x15FFC005, 0x1591B000, 0x12AAB000
ID_TAILLE, ACC_TAILLE = 0x1594B001, 0x12AAB001
OP_DONNEES, OP_FIN, ACC_BASE = 0x15950000, 0x15960000, 0x12AA0000
T_BLOC, T_FIN = 0.5, 3.0                   # attente d'accusé (s) : 0,3 ms et 67 ms mesurés


def charger(chemin):
    f = Path(chemin).read_bytes()
    if len(f) == 0x36000:
        img = f[:TAILLE]
        if set(f[TAILLE:]) - {0xFF}:
            sys.exit("ARRET : image complète non vide après le CRC")
    elif f[:4] == bytes.fromhex("5ca08200") and f[10:16] == b"CRKXY2":
        img = f[16:]
    else:
        sys.exit(f"ARRET : format inconnu ({len(f)} octets) : ni image complète 0x36000 ni fichier AOCI")
    if len(img) != TAILLE:
        sys.exit(f"ARRET : image de {len(img):#x} octets, {TAILLE:#x} attendus")
    crc_lu = struct.unpack_from("<I", img, APP_LEN)[0]
    crc = stm32_crc(img[:APP_LEN])
    if crc != crc_lu:
        sys.exit(f"ARRET : CRC calculé {crc:#010x} différent du CRC stocké {crc_lu:#010x}")
    pb = check_vectors(img)
    if pb:
        sys.exit("ARRET : " + " ; ".join(pb))
    print(f"image {chemin} : {len(img):#x} octets envoyés, CRC {crc:#010x}, SHA-256 {hashlib.sha256(img).hexdigest()[:16]}...")
    return img


def blocs(img):
    n_pleins = (len(img) - 1) // 8           # le dernier envoi (op 6) porte le reste, 1 à 8 octets
    for n in range(n_pleins):
        yield n, OP_DONNEES | n, img[8 * n:8 * n + 8]
    yield n_pleins, OP_FIN | n_pleins, img[8 * n_pleins:]


class Pcan:
    def __init__(self, canal):
        import can
        self.can = can
        self.bus = can.Bus(interface="pcan", channel=canal, bitrate=250000)   # mode actif : émet et acquitte

    def envoyer(self, ident, data):
        self.bus.send(self.can.Message(arbitration_id=ident, data=bytes(data), is_extended_id=True))

    def recevoir(self, timeout):
        m = self.bus.recv(timeout)
        return (m.arbitration_id, bytes(m.data)) if m is not None and m.is_extended_id and not m.is_error_frame else None

    def fermer(self):
        self.bus.shutdown()


class BootloaderSimule:
    """Bootloader AOCI simulé d'après la capture : accorde après quelques demandes, acquitte chaque bloc, vérifie le CRC."""
    def __init__(self):
        self.file, self.demandes, self.taille, self.mem, self.attendu = [], 0, None, bytearray(), 0
        self.ok = False

    def envoyer(self, ident, data):
        if ident == DEMANDE and bytes(data) == bytes.fromhex("5ca00200"):
            self.demandes += 1
            if self.demandes == 25:              # mise sous tension « pendant » l'attente
                self.file.append((ACCORD, b""))
        elif ident == ID_TAILLE:
            self.taille = int.from_bytes(data, "big"); self.file.append((ACC_TAILLE, b""))
        elif ident & 0xFFFF0000 in (OP_DONNEES, OP_FIN):
            n = ident & 0xFFFF
            assert n == self.attendu, f"bloc {n} reçu, {self.attendu} attendu"
            self.mem += data; self.attendu += 1
            if ident & 0xFFFF0000 == OP_FIN:
                assert len(self.mem) == self.taille, "taille finale"
                self.ok = stm32_crc(bytes(self.mem[:APP_LEN])) == struct.unpack_from("<I", self.mem, APP_LEN)[0]
                if not self.ok:
                    return                         # CRC faux : pas d'accusé final
            self.file.append((ACC_BASE | n, b""))

    def recevoir(self, timeout):
        return self.file.pop(0) if self.file else None

    def fermer(self):
        pass


def flasher(bus, img, attente, journal):
    t0 = time.time()

    def log(msg):
        ligne = f"{time.time() - t0:8.3f} s  {msg}"
        print(ligne, flush=True)
        if journal:
            journal.write(ligne + "\n"); journal.flush()

    def attendre(ident, delai):
        fin = time.time() + delai
        while time.time() < fin:
            r = bus.recevoir(max(0.0, fin - time.time()))
            if r and r[0] == ident:
                return True
        return False

    log(f"attente de l'accord du bootloader ({attente:.0f} s max) : METTRE LE CONTROLEUR SOUS TENSION")
    fin = time.time() + attente
    taille_ok = False
    while time.time() < fin and not taille_ok:
        bus.envoyer(ANNONCE, b"\x00")
        bus.envoyer(DEMANDE, bytes.fromhex("5ca00200"))
        if not attendre(ACCORD, 0.020):
            continue
        log("accord du bootloader (0x12AAB000)")
        # 26/10 : taille envoyée aussitôt après l'accord, non acquittée une fois sur trois. Le programmateur officiel
        # attend ~2,7 ms : on attend 5 ms, puis 3 essais. Sans accusé, retour à la demande (aucune donnée écrite).
        time.sleep(0.005)
        for essai in range(3):
            bus.envoyer(ID_TAILLE, len(img).to_bytes(3, "big"))
            if attendre(ACC_TAILLE, T_BLOC):
                taille_ok = True
                break
            log(f"taille non acquittée (essai {essai + 1}/3)")
    if not taille_ok:
        log("ECHEC : pas d'accord ni de taille acquittée (contrôleur mis sous tension pendant l'attente ?)")
        return False
    log(f"taille {len(img):#x} acquittée, envoi des blocs")
    total = (len(img) + 7) // 8
    for n, ident, data in blocs(img):
        bus.envoyer(ident, data)
        if not attendre(ACC_BASE | n, T_FIN if ident & 0xFFFF0000 == OP_FIN else T_BLOC):
            log(f"ECHEC : bloc {n} ({ident:#010x}) non acquitté. Couper / remettre l'alim et relancer l'outil.")
            return False
        if n % 2000 == 0 and n:
            log(f"bloc {n}/{total}")
    log(f"{total} blocs acquittés, dernier accusé reçu (CRC vérifié par le bootloader) : flash terminé")
    return True


def main():
    ap = argparse.ArgumentParser(description="Flash de l'application AOCI par le CAN (bootloader d'origine)")
    ap.add_argument("image")
    ap.add_argument("--go", action="store_true", help="émettre réellement sur le bus (sinon : vérification seule)")
    ap.add_argument("--simuler", action="store_true", help="séquence complète contre un bootloader simulé")
    ap.add_argument("--attente", type=float, default=120.0, help="attente max de l'accord du bootloader (s)")
    ap.add_argument("--canal", default="PCAN_USBBUS1")
    ap.add_argument("--journal")
    a = ap.parse_args()
    img = charger(a.image)
    if not (a.go or a.simuler):
        print(f"vérification seule : {sum(1 for _ in blocs(img))} envois prévus. Rien n'a été émis (--go pour flasher).")
        return
    bus = BootloaderSimule() if a.simuler else Pcan(a.canal)
    journal = open(a.journal, "w", encoding="utf-8") if a.journal else None
    try:
        ok = flasher(bus, img, a.attente, journal)
        if a.simuler:
            ok = ok and bus.ok and bytes(bus.mem) == img
            print("simulation :", "image reçue identique, CRC bon" if ok else "ECHEC")
    finally:
        bus.fermer()
        if journal:
            journal.close()
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
