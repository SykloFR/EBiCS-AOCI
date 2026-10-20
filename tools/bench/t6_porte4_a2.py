"""Porte 4, A2 : config active du build de banc. Adresses RAM resolues depuis l'ELF (elfsym.py).
  --flash IMAGE : (DESTRUCTIF, sur accord de Yanni) verifie MOE = 0, sauvegarde la zone appli actuelle, programme IMAGE a
                  0x08005000 par secteurs (bootloader, 0x0803B000 et 0x0803F000 non effaces), relit et compare, puis reset
                  logiciel (attendu : defaut 21 verrouille par le build de banc).
  (sans option) : lecture seule en attach, sans halt ni reset : MOE, cause du reset, defaut, MP en RAM, pages inchangees.
Usage : python tools/bench/t6_porte4_a2.py ELF DOSSIER_PREUVES [--flash IMAGE]   (sans -I : pyocd est installe en --user)"""
import hashlib, os, sys, time
sys.path.insert(0, os.path.dirname(__file__))
import elfsym
from pyocd.core.helpers import ConnectHelper
from pyocd.core.target import Target
from pyocd.flash.file_programmer import FileProgrammer

ELF, OUT = sys.argv[1], sys.argv[2]
IMAGE = sys.argv[sys.argv.index("--flash") + 1] if "--flash" in sys.argv else None
APP, APP_LEN = 0x08005000, 0x36000
BDTR = 0x40012C44                       # TIM1_BDTR (registre du composant), bit 15 = MOE
PAGE_CFG, PAGE_HALL = 0x0803F000, 0x0803B000
REF = {PAGE_CFG: ("page_0803F000.bin", 2048), PAGE_HALL: ("hall_0803B000.bin", 256)}
ATTENDU = {"MP.phase_current_max": 12, "MP.voltage_min": 1064, "MP.battery_current_max": 2000, "MP.gear_ratio": 38,
           "MP.walk_assist_current": 30, "MP.reverse": -1, "PI_iq.limit_output": 1500}
AUTRES = ["safety_fault_code_latched", "boot_reset_flags", "ui_8_PWM_ON_Flag", "bench_ol_enable", "bench_ol_active",
          "MP.MagicNumber"]
SIGNES = {"MP.voltage_min", "MP.reverse"}


def sha(b):
    return hashlib.sha256(b).hexdigest()


def moe(t):
    v = t.read32(BDTR)
    print(f"TIM1_BDTR = 0x{v:08X} -> MOE = {(v >> 15) & 1}")
    return (v >> 15) & 1


def lire(t, syms):
    res = {}
    for e, (a, n) in syms.items():
        b = bytes(t.read_memory_block8(a, n))
        res[e] = int.from_bytes(b, "little", signed=e in SIGNES)
    return res


syms = elfsym.resolve(ELF, list(ATTENDU) + AUTRES)
print(f"ELF {ELF} SHA-256 {sha(open(ELF, 'rb').read())}")
opts = {"frequency": 1000000}
if IMAGE:
    opts.update({"chip_erase": "sector", "smart_flash": False})
# Flash : connexion en halt puis reset-halt avant l'effacement. Le 20/10 en attach, l'appli tournait avec ses interruptions
# pendant que l'algorithme effacait sa table de vecteurs : HardFault dans l'algorithme (IPSR = 3), flash interrompu.
with ConnectHelper.session_with_chosen_probe(target_override="stm32f103rc", connect_mode="halt" if IMAGE else "attach",
                                             options=opts) as s:
    t = s.target
    if IMAGE:
        img = open(IMAGE, "rb").read()
        assert len(img) == APP_LEN, f"image de {len(img):#x} octets, {APP_LEN:#x} attendus"
        if moe(t):
            sys.exit("ARRET : MOE = 1 avant flash")
        t.reset_and_halt()          # coeur arrete au vecteur de reset du bootloader, peripheriques et interruptions au repos
        print("coeur :", t.get_state())
        # DBGMCU_CR bit 8 (DBG_IWDG_STOP) : gele le chien de garde de l'appli en cours (200 ms) pendant que le coeur est arrete
        t.write32(0xE0042004, t.read32(0xE0042004) | (1 << 8))
        avant = bytes(t.read_memory_block8(APP, APP_LEN))
        f = os.path.join(OUT, "app_avant_flash_A2.bin")
        if os.path.exists(f):        # ne jamais ecraser une sauvegarde existante (celle du premier essai est la bonne)
            f = os.path.join(OUT, time.strftime("app_avant_flash_A2_%Y%m%d-%H%M%S.bin"))
        open(f, "wb").write(avant)
        print(f"sauvegarde zone appli -> {f} SHA-256 {sha(avant)}")
        FileProgrammer(s).program(IMAGE, base_address=APP, file_format="bin")
        relu = bytes(t.read_memory_block8(APP, APP_LEN))
        print(f"image SHA-256 {sha(img)}\nrelue SHA-256 {sha(relu)} -> {'IDENTIQUE' if relu == img else 'DIFFERENTE'}")
        if relu != img:
            sys.exit("ARRET : relecture differente de l'image")
        t.reset(Target.ResetType.SYSRESETREQ)   # reset logiciel : le build de banc doit verrouiller le defaut 21
        time.sleep(1.5)                 # fenetre CAN du bootloader (~0,5 s) puis demarrage de l'appli (~0,6 s)
    m = moe(t)
    for adr, (nom, n) in REF.items():
        b = bytes(t.read_memory_block8(adr, n))
        ref = open(os.path.join(OUT, nom), "rb").read()
        print(f"{adr:#010x} ({n} o) SHA-256 {sha(b)[:16]}... -> {'inchangee' if b == ref else 'MODIFIEE'} par rapport a {nom}")
    v = lire(t, syms)
for e, att in ATTENDU.items():
    print(f"{e:28s} = {v[e]:6d}  attendu {att:6d}  {'OK' if v[e] == att else 'ECART'}")
for e in AUTRES:
    print(f"{e:28s} = {v[e]:#x}" if e == "boot_reset_flags" else f"{e:28s} = {v[e]}")
f = v["boot_reset_flags"]
print("cause du reset :", ", ".join(n for b, n in ((26, "broche"), (27, "mise sous tension"), (28, "logiciel"),
                                                   (29, "chien de garde"), (30, "chien de fenetre")) if f >> b & 1) or "aucune")
print("BILAN MOE :", "0, OK" if m == 0 else "1, ALERTE")
