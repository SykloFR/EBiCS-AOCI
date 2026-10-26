"""Essais T2 (commandes CAN de banc) sur le contrôleur de banc, MOTEUR DÉBRANCHÉ, alim 48 V / 0,5 A.
Commandes par le CANable (mode actif, bench_can_cmd.Banc), vérification croisée par SWD en attach (MOE, défaut, MS.u_d).
  serieA : refus sans heartbeat, refus pendant l'armement (< 500 ms), OL_SET accepté (MOE 1), plafond u_d, OL_STOP sans
           défaut, puis perte du heartbeat pendant l'essai : MOE 0 en <= 400 ms et défaut 30. Finit sur défaut 30.
  serieB : ESTOP : défaut 31, MOE 0, OL_SET ensuite refusé (statut 2). Finit sur défaut 31.
Remise sous tension entre les deux séries. Usage : python tools/bench/t17_porte4_t2.py ELF serieA|serieB"""
import os, sys, time
sys.path.insert(0, os.path.dirname(__file__))
import elfsym
from bench_can_cmd import Banc, STATUTS
from pyocd.core.helpers import ConnectHelper

ELF, SERIE = sys.argv[1], sys.argv[2]
BDTR = 0x40012C44
sym = elfsym.resolve(ELF, ["safety_fault_code_latched", "MS.u_d", "bench_ol_active"])
res = []


def rd(t, e):
    a, n = sym[e]
    return int.from_bytes(bytes(t.read_memory_block8(a, n)), "little", signed=e == "MS.u_d")


def moe(t):
    return (t.read32(BDTR) >> 15) & 1


def verdict(nom, ok, detail):
    res.append(ok)
    print(f"[{'OK' if ok else 'ECHEC'}] {nom} : {detail}", flush=True)


with ConnectHelper.session_with_chosen_probe(target_override="stm32f103rc", connect_mode="attach",
                                             options={"frequency": 4000000}) as s, Banc() as b:
    t = s.target
    elfsym.verifie_image(t, ELF)
    time.sleep(1.2)                                        # télémétrie reçue, dont la trame hello E203
    hello = b.dernier.get(0xE203, {})
    print(f"depart : MOE {moe(t)}, defaut {rd(t, 'safety_fault_code_latched')}, hello {hello}", flush=True)
    if moe(t) or rd(t, "safety_fault_code_latched"):
        sys.exit("ARRET : MOE ou defaut non nuls au depart (remise sous tension attendue)")

    if SERIE == "serieA":
        st = b.ol_set(0, 0)
        verdict("OL_SET sans heartbeat refusé", st == 1 and moe(t) == 0, f"statut {st} ({STATUTS.get(st)}), MOE {moe(t)}")
        b.demarrer_heartbeat(); time.sleep(0.2)
        st = b.ol_set(0, 0)
        verdict("OL_SET après 0,2 s de heartbeat refusé (armement 500 ms)", st == 1 and moe(t) == 0, f"statut {st}, MOE {moe(t)}")
        time.sleep(0.6)
        st = b.ol_set(0, 0); time.sleep(0.1)
        verdict("OL_SET après 0,8 s de heartbeat accepté", st == 0 and moe(t) == 1 and rd(t, "bench_ol_active") == 1,
                f"statut {st}, MOE {moe(t)}, outil {rd(t, 'bench_ol_active')}, E002 {b.dernier.get(0xE002)}")
        st = b.ol_set(0, 200); time.sleep(0.1)
        verdict("plafond u_d par CAN", st == 0 and rd(t, "MS.u_d") == 60, f"u_d demandé 200 -> MS.u_d {rd(t, 'MS.u_d')}")
        b.ol_set(0, 0); time.sleep(0.5)
        verdict("heartbeat maintient l'outil (keepalive 300 ms rafraîchi)", moe(t) == 1, f"MOE {moe(t)} après 0,5 s")
        st = b.ol_stop(); time.sleep(0.1)
        verdict("OL_STOP : arrêt sans défaut", st == 0 and moe(t) == 0 and rd(t, "safety_fault_code_latched") == 0,
                f"statut {st}, MOE {moe(t)}, défaut {rd(t, 'safety_fault_code_latched')}")
        st = b.ol_set(0, 0); time.sleep(0.1)
        avant = moe(t)
        b.arreter_heartbeat()
        t0 = time.perf_counter()
        while moe(t) and time.perf_counter() - t0 < 2.0:
            pass
        dt = (time.perf_counter() - t0) * 1000
        f = rd(t, "safety_fault_code_latched")
        verdict("perte du heartbeat : MOE 0 en <= 400 ms, défaut 30", avant == 1 and moe(t) == 0 and dt <= 400 and f == 30,
                f"MOE avant {avant}, coupure après {dt:.0f} ms, défaut {f}, E200 {b.dernier.get(0xE200)}")
    elif SERIE == "serieB":
        b.demarrer_heartbeat(); time.sleep(0.8)
        b.estop(); time.sleep(0.2)
        f = rd(t, "safety_fault_code_latched")
        verdict("ESTOP : défaut 31, MOE 0", f == 31 and moe(t) == 0, f"défaut {f}, MOE {moe(t)}")
        st = b.ol_set(0, 0); time.sleep(0.1)
        verdict("OL_SET après ESTOP refusé", st == 2 and moe(t) == 0, f"statut {st} ({STATUTS.get(st)}), MOE {moe(t)}")
print(f"\nBILAN {SERIE} : {sum(res)}/{len(res)} OK")
