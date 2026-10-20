"""Porte 4, A3 : securites du build de banc par SWD, etage de puissance NON alimente (sonde seule, pas de batterie).
Adresses RAM et points d'arret resolus depuis l'ELF ; seuls les registres du composant sont fixes.
  serie1 : (1) remise a zero du PI et CCR neutres a l'activation, plafond u_d, arret seul de l'outil vecteur fixe ;
           (2) defaut injecte entre le test du verrou et POEN = 1 (point d'arret dans pwm_enable_safe) ;
           (3) garde ISR : POEN force a 1 avec un defaut verrouille ; (4) Default_Handler : POEN = 0 puis reset.
           Fin de serie : defaut 21 verrouille (reset logiciel) -> remise sous tension avant serie2.
  serie2 : trip de phase : decalage de l'offset injecte de la phase 1 (registre IOFF0 de l'ADC2), 12 points (pas de
           coupure attendue) puis 20 points (coupure attendue, defaut 3). L'offset d'origine est restaure.
Usage : python tools/bench/t7_porte4_a3.py ELF serie1|serie2"""
import os, re, subprocess, sys, time
sys.path.insert(0, os.path.dirname(__file__))
import elfsym
from pyocd.core.helpers import ConnectHelper
from pyocd.core.target import Target

ELF, SERIE = sys.argv[1], sys.argv[2]
BDTR, CCR = 0x40012C44, (0x40012C34, 0x40012C38, 0x40012C3C)
DBGMCU_CR, DEMCR = 0xE0042004, 0xE000EDFC
NVIC_ISER0, NVIC_ISPR0 = 0xE000E100, 0xE000E200
IRQ_LIBRE = 6                       # EXTI0 : vecteur sur Default_Handler, ligne non configuree
ADC2_IOFF0 = 0x40013C00 + 0x14      # phase 1 = ADC2 voie injectee 0 (main.c, lecture i16_ph1_current)

V = ["safety_fault_code_latched", "ui_8_PWM_ON_Flag", "bench_ol_enable", "bench_ol_active", "bench_ol_keepalive",
     "bench_ol_ud", "bench_ol_angle", "PI_iq.out", "PI_iq.integral_part", "PI_id.out", "PI_id.integral_part", "MS.u_d",
     "MS.u_q", "boot_reset_flags", "i16_ph1_current", "i16_ph2_current", "i16_ph3_current", "MS.char_dyn_adc_state"]
SIGNES = {"PI_iq.out", "PI_iq.integral_part", "PI_id.out", "PI_id.integral_part", "MS.u_d", "MS.u_q", "bench_ol_ud",
          "i16_ph1_current", "i16_ph2_current", "i16_ph3_current", "bench_ol_angle"}
sym = elfsym.resolve(ELF, V + ["Default_Handler"])
dis = subprocess.run(["arm-none-eabi-objdump", "-d", "--no-show-raw-insn", ELF], capture_output=True, text=True).stdout
pes = re.search(r"<pwm_enable_safe>:\n(.*?)\n\n", dis, re.S).group(1)
BP_POEN = int(re.search(r"orr\S*\s+r\d+, r\d+, #32768.*?\n\s*([0-9a-f]+):\s+str", pes, re.S).group(1), 16)
DH = sym["Default_Handler"][0] & ~1
ok_total = []


def rd(t, e):
    a, n = sym[e]
    return int.from_bytes(bytes(t.read_memory_block8(a, n)), "little", signed=e in SIGNES)


def wr(t, e, v):
    a, n = sym[e]
    t.write_memory_block8(a, list(int(v).to_bytes(n, "little", signed=v < 0)))
    t.flush()                                   # pyOCD met les ecritures en file : les executer maintenant


def moe(t):
    return (t.read32(BDTR) >> 15) & 1


def verdict(nom, cond, detail):
    ok_total.append(cond)
    print(f"[{'OK' if cond else 'ECHEC'}] {nom} : {detail}")


def set_bp(t, addr):
    """pyOCD n'ecrit le FPB qu'a la reprise du coeur : arret bref, pose, reprise (IWDG gele)."""
    t.halt()
    t.set_breakpoint(addr)
    t.resume()


def wait_halt(t, timeout=2.0):
    t0 = time.time()
    while t.get_state() != Target.State.HALTED:
        if time.time() - t0 > timeout:
            return False
        time.sleep(0.005)
    return True


with ConnectHelper.session_with_chosen_probe(target_override="stm32f103rc", connect_mode="attach",
                                             options={"frequency": 1000000}) as s:
    t = s.target
    t.write32(DBGMCU_CR, t.read32(DBGMCU_CR) | (1 << 8))     # IWDG gele pendant les arrets du coeur
    t.write32(DEMCR, t.read32(DEMCR) & ~1)                    # pas d'arret sur reset (vector catch)
    print(f"depart : MOE {moe(t)}, defaut {rd(t, 'safety_fault_code_latched')}, cause reset {rd(t, 'boot_reset_flags'):#x}")
    if rd(t, "safety_fault_code_latched") != 0 or moe(t):
        sys.exit("ARRET : defaut deja verrouille ou MOE = 1 au depart (remise sous tension attendue)")

    if SERIE == "serie1":
        # (1) PI remis a zero et CCR neutres a l'activation ; plafond u_d ; arret seul de l'outil
        for e, v in (("PI_iq.out", 500), ("PI_iq.integral_part", 12345678), ("PI_id.out", -300), ("PI_id.integral_part", -999)):
            wr(t, e, v)
        wr(t, "bench_ol_ud", 0); wr(t, "bench_ol_angle", 0); wr(t, "bench_ol_keepalive", 8000); wr(t, "bench_ol_enable", 1)
        time.sleep(0.05)
        pi = [rd(t, e) for e in ("PI_iq.out", "PI_iq.integral_part", "PI_id.out", "PI_id.integral_part")]
        ccr = [t.read32(a) for a in CCR]
        verdict("activation : PI remis a zero", pi == [0, 0, 0, 0], f"PI_iq.out/int, PI_id.out/int = {pi} (avant : 500, 12345678, -300, -999)")
        verdict("activation : CCR neutres et MOE", ccr == [1000] * 3 and moe(t) == 1 and rd(t, "bench_ol_active") == 1,
                f"CCR {ccr}, MOE {moe(t)}, outil actif {rd(t, 'bench_ol_active')} (pilotes non alimentes)")
        wr(t, "bench_ol_ud", 500); wr(t, "bench_ol_keepalive", 8000); time.sleep(0.02)
        verdict("plafond u_d", rd(t, "MS.u_d") == 60, f"u_d demande 500 -> MS.u_d = {rd(t, 'MS.u_d')} (plafond 60)")
        wr(t, "bench_ol_ud", 0)
        t0 = time.time()
        while moe(t) and time.time() - t0 < 4:
            time.sleep(0.05)
        dt = time.time() - t0
        verdict("arret seul de l'outil sans rafraichissement", moe(t) == 0 and rd(t, "bench_ol_enable") == 0 and 1.0 < dt < 3.0,
                f"MOE 0 apres {dt:.2f} s (2 s attendues), enable {rd(t, 'bench_ol_enable')}, PWM_ON {rd(t, 'ui_8_PWM_ON_Flag')}")

        # (2) defaut injecte entre le test du verrou et l'ecriture de POEN
        set_bp(t, BP_POEN)
        wr(t, "bench_ol_keepalive", 8000); wr(t, "bench_ol_enable", 1)
        hit = wait_halt(t)
        pc = t.read_core_register("pc") if hit else 0
        avant = moe(t)
        wr(t, "safety_fault_code_latched", 3)                     # defaut pose apres le test, avant POEN = 1
        t.remove_breakpoint(BP_POEN)
        t.resume()
        time.sleep(0.01)
        verdict("defaut entre test et activation", hit and pc == BP_POEN and avant == 0 and moe(t) == 0
                and rd(t, "safety_fault_code_latched") == 3,
                f"arret a {pc:#x} (attendu {BP_POEN:#x}), POEN avant {avant} -> apres {moe(t)}, defaut {rd(t, 'safety_fault_code_latched')}, "
                f"PWM_ON {rd(t, 'ui_8_PWM_ON_Flag')}, outil actif {rd(t, 'bench_ol_active')}")
        wr(t, "bench_ol_enable", 0)

        # (3) garde ISR : defaut verrouille, POEN force a 1 par le debogueur
        t.write32(BDTR, t.read32(BDTR) | 0x8000)
        t.flush()
        time.sleep(0.002)
        verdict("garde ISR (defaut verrouille, POEN force)", moe(t) == 0, f"BDTR = {t.read32(BDTR):#06x} 2 ms apres le forcage")

        # (4) Default_Handler : POEN = 0 puis reset
        set_bp(t, DH)
        t.write32(NVIC_ISER0, 1 << IRQ_LIBRE)
        t.write32(NVIC_ISPR0, 1 << IRQ_LIBRE)
        hit = wait_halt(t)
        t.remove_breakpoint(DH)
        t.write32(BDTR, t.read32(BDTR) | 0x8000)                # MOE force a 1 a l'entree du handler
        bp2 = DH + 0x0A                                          # dsb qui suit l'ecriture de BDTR
        t.set_breakpoint(bp2)
        t.resume()
        hit2 = wait_halt(t)
        poen = moe(t)
        t.remove_breakpoint(bp2)
        verdict("Default_Handler coupe MOE", hit and hit2 and poen == 0, f"entree {hit}, POEN apres le str = {poen}")
        t.resume()
        time.sleep(2.0)                                          # reset, fenetre bootloader 0,5 s, demarrage appli
        f = rd(t, "boot_reset_flags")
        verdict("Default_Handler redemarre et verrouille", (f >> 28) & 1 == 1 and rd(t, "safety_fault_code_latched") == 21
                and moe(t) == 0, f"cause reset {f:#x} (bit 28 = logiciel), defaut {rd(t, 'safety_fault_code_latched')}, MOE {moe(t)}")

    elif SERIE == "serie2":
        orig = t.read32(ADC2_IOFF0) & 0xFFF
        print(f"IOFF0 ADC2 (phase 1) = {orig}, ph1 au repos = {rd(t, 'i16_ph1_current')}")
        try:
            t.write32(ADC2_IOFF0, orig - 12)
            t.flush()
            time.sleep(0.1)
            ph = rd(t, "i16_ph1_current")
            verdict("12 points : pas de coupure", rd(t, "safety_fault_code_latched") == 0,
                    f"ph1 = {ph} (CURRENT_SIGN -1 : -12 attendu), defaut {rd(t, 'safety_fault_code_latched')}")
            t.write32(ADC2_IOFF0, orig - 20)
            t.flush()
            time.sleep(0.1)
            verdict("20 points : coupure", rd(t, "safety_fault_code_latched") == 3 and moe(t) == 0,
                    f"defaut {rd(t, 'safety_fault_code_latched')}, MOE {moe(t)}, etat ADC dyn {rd(t, 'MS.char_dyn_adc_state')}")
        finally:
            t.write32(ADC2_IOFF0, orig)
            t.flush()
            print(f"IOFF0 restaure = {t.read32(ADC2_IOFF0) & 0xFFF}")
        if rd(t, "safety_fault_code_latched"):                  # garde ISR rejouee avec ecriture effective (flush)
            t.write32(BDTR, t.read32(BDTR) | 0x8000)
            t.flush()
            time.sleep(0.002)
            verdict("garde ISR (defaut 3 verrouille, POEN force)", moe(t) == 0, f"BDTR = {t.read32(BDTR):#06x} 2 ms apres le forcage")
    t.write32(DBGMCU_CR, t.read32(DBGMCU_CR) & ~(1 << 8))
print(f"\nBILAN {SERIE} : {sum(ok_total)}/{len(ok_total)} OK")
