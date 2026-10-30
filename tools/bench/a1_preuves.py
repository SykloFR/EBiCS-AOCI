"""Preuves du build de banc porte 4 (A1, revue Fangyan point 13). Lecture seule : n'ecrit que le rapport.
Usage depuis la racine du depot, apres  mingw32-make OS= CORE=m3 BENCH=1 CURRENT_SIGN=1 aoci :
    python -I tools/bench/a1_preuves.py build-m3-banc [CURRENT_SIGN=1] > build-m3-banc/preuves_a1.txt
Contenu : empreintes, size, macros de banc effectives, symboles banc, desassemblage des points de securite,
table de vecteurs (entrees sur Default_Handler croisees avec les IRQ activees), usage de pile (.su)."""
import hashlib, os, re, struct, subprocess, sys

B = sys.argv[1] if len(sys.argv) > 1 else "build-m3-banc"
SIGNE = sys.argv[2] if len(sys.argv) > 2 else "1"   # meme valeur que le build (F6 : 1)
ELF, BIN = f"{B}/ebics.elf", f"{B}/ebics.bin"
ROUTE = B.rstrip("/").endswith("route")          # build route (decision du 27/10) : memes preuves, seuils de production
IMG = f"{B}/ebics_ROUTE_aoci_full.bin" if ROUTE else f"{B}/ebics_BANC_aoci_full.bin"


def run(*cmd):
    return subprocess.run(cmd, capture_output=True, text=True, check=True).stdout


def section(t):
    print(f"\n=== {t} ===")


section("Empreintes SHA-256")
for f in (ELF, BIN, IMG):
    print(f"{hashlib.sha256(open(f, 'rb').read()).hexdigest()}  {f}")
img = open(IMG, "rb").read()
print(f"image {len(img):#x} octets, mot a 0x0803AFC0 (CRC lu par le bootloader) = {struct.unpack_from('<I', img, 0x35FC0)[0]:#010x}")

section("arm-none-eabi-size")
print(run("arm-none-eabi-size", ELF).strip())

section("Macros effectives (preprocesseur, memes options que le build banc)")
cflags = ["-mcpu=cortex-m3", "-mthumb", "-mfloat-abi=soft", "-DGD32F10X_HD", "-DUSE_STDPERIPH_DRIVER", "-DARM_MATH_CM3",
          "-DHXTAL_VALUE=12000000U", "-DBENCH_BUILD", f"-DCURRENT_SIGN={SIGNE}"] + (["-DROUTE_BUILD"] if ROUTE else []) + [ "-Iport_f10x", "-Iinc", "-IFirmware_F10x/CMSIS",
          "-IFirmware_F10x/CMSIS/GD/GD32F10x/Include", "-IFirmware_F10x/GD32F10x_standard_peripheral/Include"]
macros = run("arm-none-eabi-gcc", *cflags, "-dM", "-E", "src/main.c")
want = r"^#define (BENCH_\w+|PH_CURRENT_MAX|PH_CURRENT_TRIP|PH_TRIP_PERIODS|BATTERYCURRENT_MAX|VOLTAGE_MIN|" \
       r"TEMP_SENSOR_REQUIRED|U_MAX_BENCH|_U_MAX|CURRENT_SIGN|_T|DYNAMIC_ADC_THRESHOLD|REVERSE|LEGALFLAG|GEAR_RATIO|" \
       r"CAL_PONT_\w+|VARIANTE_ADDR|PH_COUNTS_PER_A_\w+|TRIGGER_DEFAULT|TQFILTER|CZ_N)\b.*"
for line in sorted(l for l in macros.splitlines() if re.match(want, l)):
    print(line)

section("Parametres figes en route (28/10) : preprocesseur, memes options")
pp_can = run("arm-none-eabi-gcc", *cflags, "-E", "-P", "src/CAN_Display.c")
pp_main = run("arm-none-eabi-gcc", *cflags, "-E", "-P", "src/main.c")
chemins = {"0x3203 -> speedLimitx100": "speedLimitx100=receive_message" in pp_can,
           "multitrame -> parse_DPparams": "parse_DPparams(MP)" in pp_can,
           "0x62D9 -> TS_coeff": "TS_coeff=receive_message" in pp_can,
           "mode hors route -> offroadflag": "offroadflag=!MS.offroadflag" in pp_main}
for k, v in chemins.items():
    print(f"   {k:32s} : {'present' if v else 'absent'}")
socle = {"bride (limite - 2) -> (limite + 2)": "socle_bride(MS.i_q_setpoint_temp" in pp_main,
         "plafond batterie par niveau": "MP.battery_current_max=socle_ibat_niveau_mA(" in pp_main,
         "phase 1,75 x batterie du niveau": "phase_current_max_scaled=socle_phase_niveau_pts(" in pp_main,
         "niveau demande ecrit par l'afficheur": "route_niveau_demande=MS->assist_level" in pp_can}
for k, v in socle.items():
    print(f"   socle (28/10) {k:38s} : {'present' if v else 'absent'}")
timer_l5 = "socle_l5_pas(&route_l5" in pp_main
print(f"   timer L5 du socle (2 min / 8 min) dans le firmware : {'present' if timer_l5 else 'absent'}")
img_pi = open(IMG, "rb").read()   # limit_i (offset 8) et limit_output (offset 10) ecrits par un seul mot
umax = 1800 if ROUTE else 1500   # lot B3 (30/10) : U_MAX_BENCH 1800 en route
mot_34, mot_max = struct.pack("<I", (umax << 16) | (umax * 3 // 4)), struct.pack("<I", (umax << 16) | umax)
print(f"   mots limit_i | limit_output << 16 dans l'image : {umax * 3 // 4}|{umax} x{img_pi.count(mot_34)}, {umax}|{umax} x{img_pi.count(mot_max)}")
plafond_iq = img_pi.count(mot_max) >= 1 and img_pi.count(mot_34) >= 1   # i_q a _U_MAX, i_d garde 3/4
print(f"   PI de i_q : integrale plafonnee a _U_MAX (29/10) : {'oui' if plafond_iq else 'non (3/4)'} ; PI de i_d a 3/4 : {'oui' if img_pi.count(mot_34) else 'non'}")
if ROUTE:
    print("route : integrale du PI de i_q jusqu'a _U_MAX :", "OK" if plafond_iq else "A VERIFIER")
else:
    print("banc : integrale du PI de i_q a 3/4 (inchange) :", "OK" if not plafond_iq else "A VERIFIER")
if ROUTE:
    print("route : socle de conformite present (bride, plafonds par niveau, niveau demande), timer L5 retire (30/10) :",
          "OK" if all(socle.values()) and not timer_l5 else "A VERIFIER")
else:
    print("banc : pas de socle de conformite :", "OK" if not any(socle.values()) and not timer_l5 else "A VERIFIER")
if ROUTE:
    print("route : les quatre chemins qui changent la bride ou les parametres en marche sont absents :",
          "OK" if not any(chemins.values()) else "A VERIFIER")
else:
    print("banc : les quatre chemins restent presents (banc inchange) :", "OK" if all(chemins.values()) else "A VERIFIER")

section("Chaine de couple (29/10) : TQFILTER et fenetre du zero adaptatif CZ_N")
tq = re.search(r"^#define TQFILTER (\d+)", macros, re.M)
czn = re.search(r"^#define CZ_N (\d+)u", macros, re.M)
tq, czn = (int(tq.group(1)) if tq else None), (int(czn.group(1)) if czn else None)
pp_parser = run("arm-none-eabi-gcc", *cflags, "-E", "-P", "src/parser.c")
reglages = sorted(set(re.findall(r"assist_settings\[(?:k\+1|5)\]\[2\]=(\d+)", pp_parser)))
taille_cz = [int(l.split()[1], 16) for l in run("arm-none-eabi-nm", "-S", ELF).splitlines() if l.endswith(" couple_zero")]
print(f"   TQFILTER {tq} ; CZ_N {czn} ; parser.c : assist_settings[k][2] = {reglages} ; couple_zero {taille_cz} octets")
# le zero adaptatif n'existe qu'en route (main.c, bloc ROUTE_BUILD des includes) : absent du banc
ok_couple = (tq, czn, reglages, taille_cz) == ((5, 48, ["5"], [102]) if ROUTE else (4, None, ["4"], []))
print("route : filtre 1/32 et zero sur 48 impulsions :" if ROUTE else "banc : filtre 1/16 inchange, pas de zero adaptatif :",
      "OK" if ok_couple else "A VERIFIER")

section("Loi type OSF (29/10) : profil par niveau et plancher de cadence")
m_prof = re.search(r"route_profil_niveau\[5\] = \{([^}]*)\}", pp_parser)
profil = [int(x) for x in m_prof.group(1).split(",")] if m_prof else None
plancher = "loi_osf_plancher_sur(MS.i_q_setpoint_temp, MS.cadence, ui16_erps, MS.torque_on_crank, couple_zero.zero)" in pp_main   # 30/10
reprise30 = "couple_zero.zero+30&&PAS_counter" in pp_main
profil_ebics = "assist_profile[k][l]=(k+1)*20" in pp_parser
print(f"   profil route {profil} ; profil EBiCS (k+1) x 20 present : {profil_ebics} ; plancher (vitesse moteur, sous appui) dans update_setpoint : {plancher} ; reprise du couple a zero + 30 mV : {reprise30}")
if ROUTE:
    print("route : profil 31 / 43 / 57 / 67 / 110 et plancher de cadence :",
          "OK" if profil == [31, 43, 57, 67, 110] and plancher and reprise30 and not profil_ebics else "A VERIFIER")
else:
    print("banc : profil EBiCS et pas de plancher (inchange) :",
          "OK" if profil is None and not plancher and not reprise30 and profil_ebics else "A VERIFIER")

section("E100 (30/10) : courants vus par le trip en route")
e100_trip = "bench_can_sample(route_ph_trip[0], route_ph_trip[1], route_ph_trip[2])" in pp_main
print(f"   E100 alimentee par les entrees du trip : {'oui' if e100_trip else 'non'}")
print("route : E100 = courants vus par le trip :" if ROUTE else "banc : E100 = courants bruts (inchange) :",
      "OK" if e100_trip == ROUTE else "A VERIFIER")

section("Mesure dynamique robuste (30/10) : placement de la mesure apres la FOC, sur S(k-1) et S(k)")
robuste = "dyn_decision_robuste(route_st_prec, sn," in pp_main
ancien = "dyn_adc_state(q31_rotorposition_absolute);" in pp_main
tenue = "route_tenue ? route_ph_val : f4_mes" in pp_main
print(f"   dyn_decision_robuste appelee : {robuste} ; dyn_adc_state appelee par l'ISR : {ancien} ; trip sur courants tenus : {tenue}")
print("route : placement robuste, ancien appel retire, trip sur courants valides :" if ROUTE else "banc : dyn_adc_state d'EBiCS (inchange) :",
      "OK" if ((robuste and not ancien and tenue) if ROUTE else (ancien and not robuste)) else "A VERIFIER")

section("Forte modulation (30/10) : avance du declenchement dynamique dans dyn_adc_state")
avances = sorted(set(re.findall(r"\(switchtime\[[012]\]-(\d+)\)", pp_main)))
print(f"   avances du declenchement dynamique dans dyn_adc_state : {avances}")
print("route : avance 65 sur les trois phases :" if ROUTE else "banc : avance 10 d'EBiCS (inchange) :",
      "OK" if avances == (["65"] if ROUTE else ["10"]) else "A VERIFIER")

section("Temperature controleur (29/10) : table NTC d'origine en route, T_NTC ailleurs")
nm_t = run("arm-none-eabi-nm", ELF)
a_main, t_main = [(int(l.split()[0], 16), int(l.split()[1], 16)) for l in run("arm-none-eabi-nm", "-S", ELF).splitlines()
                  if l.endswith(" main")][0]
dis_main = run("arm-none-eabi-objdump", "-d", "--no-show-raw-insn", f"--start-address={a_main:#x}",
               f"--stop-address={a_main + t_main:#x}", ELF)
table = " ntc_aoci_table" in nm_t
appel_tntc = "<T_NTC>" in dis_main
print(f"   ntc_aoci_table en flash : {'oui' if table else 'non'} ; main appelle T_NTC : {'oui' if appel_tntc else 'non'}")
if ROUTE:
    print("route : temperature par la table d'origine (table presente, T_NTC non appelee par main) :",
          "OK" if table and not appel_tntc else "A VERIFIER")
else:
    print("banc : T_NTC inchangee (appelee par main, pas de table) :", "OK" if appel_tntc and not table else "A VERIFIER")

section("Symboles du build banc (nm)")
nm = run("arm-none-eabi-nm", "-S", "--size-sort", ELF)
syms = {}
for l in nm.splitlines():
    p = l.split()
    if len(p) == 4:
        syms[p[3]] = (int(p[0], 16), int(p[1], 16), p[2])
for s in ("bench_ol_vector", "bench_ol_enable", "bench_ol_ud", "bench_ol_angle", "bench_ol_keepalive", "bench_ol_active",
          "boot_reset_flags", "pwm_enable_safe", "pwm_disable", "autodetect", "safety_check_phase_currents",
          "offset_cal_pont_50", "variante_init", "offset_pont_ph", "offset_cal_pont", "bench_variante", "safety_ph_trip",
          "bench_ph_max", "bench_walk_max"):
    v = syms.get(s)
    print(f"{s:28s} " + (f"{v[0]:#010x} taille {v[1]:4d} ({v[2]})" if v else "absent (inline ou retire par l'editeur de liens)"))

section("Desassemblage des points de securite")
dis = run("arm-none-eabi-objdump", "-d", "--no-show-raw-insn", ELF)


def func(name, maxlines=40):
    m = re.search(rf"^[0-9a-f]+ <{re.escape(name)}>:\n(.*?)(?=\n\n)", dis, re.S | re.M)
    if not m:
        print(f"<{name}> introuvable")
        return ""
    body = m.group(1).splitlines()
    print(f"<{name}> ({len(body)} instructions)")
    for l in body[:maxlines]:
        print("   " + l.strip())
    if len(body) > maxlines:
        print(f"   ... ({len(body) - maxlines} de plus)")
    return m.group(1)


func("write_virtual_eeprom", 6)
for s in ("read_virtual_eeprom", "fmc_erase_pages", "fmc_program_hall_angles", "gd_eval_key_init"):
    print(f"{s:24s} : " + ("present" if s in syms else "absent du binaire (plus aucun appelant)"))
# Default_Handler partage son adresse avec les alias faibles : objdump l'etiquette sous un autre nom, on lit par adresse
a = syms["Default_Handler"][0]
print(f"<Default_Handler> @ {a:#010x}")
print(run("arm-none-eabi-objdump", "-d", "--no-show-raw-insn", f"--start-address={a:#x}", f"--stop-address={a + 0x28:#x}",
          ELF).split("Disassembly of section .text:")[-1].strip())
sc = func("safety_check_phase_currents", 40)

def lit_adresse(corps, sym):
    """vrai si le pool litteral de la fonction contient l'adresse de la variable sym (.word 0x...)"""
    v = syms.get(sym)
    return bool(v) and re.search(rf"\.word\s+0x{v[0]:08x}\b", corps) is not None


# correctif F2 : le seuil n'est plus une constante (cmp #15) mais la variable safety_ph_trip, 16 pour la variante 2
print("seuil lu dans safety_ph_trip (correctif F2) :", "OK" if lit_adresse(sc, "safety_ph_trip") else "A VERIFIER")
func("bench_ol_vector", 16)

section("Correctifs F1 / F2 (26/10/2026)")
vi = func("variante_init", 40) if "variante_init" in syms else func("main", 0)   # integree dans main (inline)
print("variante_init lit 0x0803D800 :", "OK" if re.search(r"\.word\s+0x0803d800\b", vi) else "A VERIFIER")
for v, nom, ech in ((2, "R = 1,5", 314), (1, "R = 3,5", 733)):
    t, m, w = 16 * ech // 314, 12 * ech // 314, 5 * ech // 314
    print(f"variante {v} ({nom}, {ech / 100} pts/A) : trip {t} pts = {t * 100 / ech:.1f} A, max {m} pts = "
          f"{m * 100 / ech:.1f} A, walk {w} pts = {w * 100 / ech:.1f} A")
func("offset_cal_pont_50", 90)
isr = func("ADC0_1_IRQHandler", 0)
print("garde F1 dans l'ISR (lit offset_cal_pont) :", "OK" if lit_adresse(isr, "offset_cal_pont") else "A VERIFIER")
cal = func("calibrate_current_offsets", 0)
section("Diagnostic du demarrage du pont et precharge F3 (26/10/2026)")
for s in ("cap_ph", "cap_n", "cap_trip", "cap_seq", "bench_precharge_ms"):
    v = syms.get(s)
    print(f"{s:20s} " + (f"{v[0]:#010x} taille {v[1]:4d}" if v else "absent"))
print("capture dans l'ISR (lit cap_n) :", "OK" if lit_adresse(isr, "cap_n") else "A VERIFIER")
pes = func("pwm_enable_safe", 70) if "pwm_enable_safe" in syms else ""
print("pwm_enable_safe lit bench_precharge_ms :", "OK" if lit_adresse(pes, "bench_precharge_ms") else "A VERIFIER (inline ?)")
print("pwm_enable_safe ecrit CHCTL2 (TIMER0 0x40012c20) :",
      "OK" if re.search(r"\.word\s+0x40012c(00|20)\b", pes) else "A VERIFIER")
section("Correctif F4 : retrait du mode commun des trois voies (26/10/2026)")
# division par 3 dans l'ISR : sdiv par une constante 3, ou multiplication par l'inverse (0x55555556)
print("division par 3 dans ADC0_1_IRQHandler :",
      "OK" if re.search(r"0x55555556|sdiv", isr) else "A VERIFIER")
section("Journal des fronts Hall (26/10/2026)")
t2 = func("TIMER2_IRQHandler", 0)
print("TIMER2_IRQHandler lit hj_gel et ecrit hj_case :",
      "OK" if lit_adresse(t2, "hj_gel") and lit_adresse(t2, "hj_case") else "A VERIFIER")
pd = func("pwm_disable", 0) if "pwm_disable" in syms else ""
print("pwm_disable fige le journal (hj_gel) :", "OK" if lit_adresse(pd, "hj_gel") else "A VERIFIER (inline ?)")
if "offset_cal_pont_50" in syms:
    print("calibrate_current_offsets appelle offset_cal_pont_50 :", "OK" if "<offset_cal_pont_50>" in cal else "A VERIFIER")
else:
    print("offset_cal_pont_50 integree (inline) : garde lue dans calibrate_current_offsets :",
          "OK" if lit_adresse(cal, "offset_cal_pont") else "A VERIFIER")

section("Sens du walk (27/10/2026, t24) : REVERSE +1 et angle a l'arret selon le sens")
mrev = re.search(r"^#define REVERSE (-?\d+)", macros, re.M)
print("REVERSE du build de banc = +1 :", "OK" if mrev and mrev.group(1) == "1" else f"A VERIFIER ({mrev and mrev.group(1)})")
gs = func("get_standstill_position", 0) if "get_standstill_position" in syms else func("main", 0)
halls = ("Hall_13", "Hall_32", "Hall_26", "Hall_64", "Hall_45", "Hall_51")
print("get_standstill_position lit MP (reverse) et les six Hall_xy :",
      "OK" if lit_adresse(gs, "MP") and all(lit_adresse(gs, h) for h in halls) else "A VERIFIER (inline ?)")
print("simulation sur les tables du depot : python -I tools/bench/simu_sens_patch.py (critere : couple avant >= 0,80)")
print("premier front (27/10) : get_standstill_position ecrit ui8_hall_state_old :",
      "OK" if lit_adresse(gs, "ui8_hall_state_old") else "A VERIFIER (inline ?)")

section("Capture avant trip (27/10/2026, E20B)")
print("ADC0_1_IRQHandler lit pt_gel et ecrit pt_ph / pt_st :",
      "OK" if all(lit_adresse(isr, s) for s in ("pt_gel", "pt_ph", "pt_st")) else "A VERIFIER")
print("pwm_disable fige la capture (pt_gel) :", "OK" if lit_adresse(pd, "pt_gel") else "A VERIFIER (inline ?)")
print("pwm_enable_safe relance la capture (pt_seq) :", "OK" if lit_adresse(pes, "pt_seq") else "A VERIFIER (inline ?)")
print("capture enrichie : ADC0_1_IRQHandler ecrit pt_ang / pt_ud / pt_uq / pt_x :",
      "OK" if all(lit_adresse(isr, s) for s in ("pt_ang", "pt_ud", "pt_uq", "pt_x")) else "A VERIFIER")
print("pepins d'angle (27/10) : ADC0_1_IRQHandler compte pepins_tenus :", "OK" if lit_adresse(isr, "pepins_tenus") else "A VERIFIER")
a_isr, t_isr = syms["ADC0_1_IRQHandler"][0], syms["ADC0_1_IRQHandler"][1]
isr_complet = run("arm-none-eabi-objdump", "-d", "--no-show-raw-insn", f"--start-address={a_isr:#x}",
                  f"--stop-address={a_isr + t_isr:#x}", ELF)          # toute la fonction, pools litteraux compris
print("   lit TIMER2_INTF (base TIMER2 0x40000400, decalage #16) :",
      "OK" if re.search(r"\.word\s+0x40000400\b", isr_complet) and re.search(r"#16\]", isr_complet) else "A VERIFIER")
print("capture avant F4 (27/10) : ADC0_1_IRQHandler ecrit pt_av :", "OK" if lit_adresse(isr_complet, "pt_av") else "A VERIFIER")
a_t2, t_t2 = syms["TIMER2_IRQHandler"][0], syms["TIMER2_IRQHandler"][1]
t2_complet = run("arm-none-eabi-objdump", "-d", "--no-show-raw-insn", f"--start-address={a_t2:#x}",
                 f"--stop-address={a_t2 + t_t2:#x}", ELF)
print("largeur du secteur (27/10) : TIMER2_IRQHandler ecrit secteur_q16, ADC0_1_IRQHandler le lit :",
      "OK" if lit_adresse(t2_complet, "secteur_q16") and lit_adresse(isr_complet, "secteur_q16") else "A VERIFIER")
if os.path.exists("inc/bench_hall_rotation.h"):           # table en rotation prioritaire (main.c)
    hv = {k: int(x) & 0xFFFFFFFF for k, x in re.findall(r"BENCH_HALL_ROT_(\d\d) \((-?\d+)\)",
                                                         open("inc/bench_hall_rotation.h", encoding="ascii").read())}
else:
    hv = {k: int(x) & 0xFFFFFFFF for k, x in re.findall(r"BENCH_HALL_(\d\d) \((-?\d+)\)",
                                                         open("inc/bench_hall_angles.h", encoding="utf-8").read())}
larg = {e: ((hv[a] - hv[b]) & 0xFFFFFFFF) >> 16 for e, (a, b) in
        {4: ("45", "64"), 5: ("51", "45"), 1: ("13", "51"), 3: ("32", "13"), 2: ("26", "32"), 6: ("64", "26")}.items()}
print("   largeurs attendues (etat : q16 / deg) :", {e: (q, round(q * 360 / 65536, 1)) for e, q in larg.items()},
      "somme", sum(larg.values()), "(65536 = 360 deg)")
print("borne de l'interpolation (27/10) : ADC0_1_IRQHandler compte bornes_interp et lit uint32_tics_filtered :",
      "OK" if lit_adresse(isr_complet, "bornes_interp") and lit_adresse(isr_complet, "uint32_tics_filtered") else "A VERIFIER")
rpi = func("runPIcontrol", 0)
print("runPIcontrol ecrit pt_cercle (limitation en cercle) :", "OK" if lit_adresse(rpi, "pt_cercle") else "A VERIFIER")
for s in ("pt_ph", "pt_st", "pt_ang", "pt_ud", "pt_uq", "pt_x", "pt_av"):
    v = syms.get(s)
    print(f"{s:10s} " + (f"{v[0]:#010x} taille {v[1]} octets" if v else "absent"))

section("Prechargement des CCR (27/10/2026) : timer0_config")
a_t0, t_t0 = syms["timer0_config"][0], syms["timer0_config"][1]
t0 = run("arm-none-eabi-objdump", "-d", "--no-show-raw-insn", f"--start-address={a_t0:#x}",
         f"--stop-address={a_t0 + t_t0:#x}", ELF).splitlines()


def valeur(reg, avant):
    """valeur immediate du registre reg au point avant (dernier movs/mov reg, #N, en suivant mov reg, rY)"""
    for l in reversed(t0[:avant]):
        m = re.search(rf"\bmovs?(?:\.w)?\s+{reg},\s+(#\d+|r\d+)", l)
        if m:
            return int(m.group(1)[1:]) if m.group(1).startswith("#") else valeur(m.group(1), t0.index(l))
    return None


appels = []
for k, l in enumerate(t0):
    if "<timer_channel_output_shadow_config>" in l:
        args = {}
        for r in ("r0", "r1", "r2"):
            args[r] = valeur(r, k)
        appels.append((args["r1"], args["r2"]))
print("appels (canal, OCxPE) :", appels)
ok_ccr = sorted(appels) == [(0, 8), (1, 8), (2, 8), (3, 0)]
print("CH0-CH2 precharges (0x08), CH3 sans prechargement (0x00) :", "OK" if ok_ccr else "A VERIFIER")

section("Table Hall du sens de marche (27/10/2026) : valeurs initiales de Hall_xy dans .data")
dump = run("arm-none-eabi-objdump", "-s", "-j", ".data", ELF)
octets = {}
for l in dump.splitlines():
    m = re.match(r"^\s*([0-9a-f]{8})\s+((?:[0-9a-f]{2,8}\s){1,4})", l)
    if m:
        base = int(m.group(1), 16)
        hexa = "".join(m.group(2).split())
        for i in range(0, len(hexa), 2):
            octets[base + i // 2] = int(hexa[i:i + 2], 16)
rot_h = "inc/bench_hall_rotation.h"
attendu = {}
if os.path.exists(rot_h):
    attendu = {k: int(v) for k, v in re.findall(r"BENCH_HALL_ROT_(\d\d) \((-?\d+)\)", open(rot_h, encoding="ascii").read())}
ok_t = bool(attendu)
for k in ("13", "32", "26", "64", "45", "51"):
    a = syms.get(f"Hall_{k}", (None,))[0]
    v = int.from_bytes(bytes(octets.get(a + i, 0) for i in range(4)), "little", signed=True) if a else None
    ok_t &= v is not None and v == attendu.get(k)
    print(f"   Hall_{k} = {v} ({(v % 2**32) * 360 / 2**32:.1f} deg) ; en-tete rotation : {attendu.get(k)}")
print("valeurs initiales = bench_hall_rotation.h :", "OK" if ok_t else "A VERIFIER (en-tete absent ou valeurs differentes)")

section("Echelle de courant (decision de revue du 27/10/2026)")
mv = {k: v for k, v in re.findall(r"^#define (TRIGGER_OFFSET_ADC_DYN|U_MAX_BENCH|PH_CURRENT_TRIP|PH_CURRENT_MAX|BENCH_WALK_ILIM_MAX|BRIDE_ERPS_DEBUT|BRIDE_ERPS_FIN|SPEEDLIMIT|ROUTE_NIVEAU_MAX|ROUTE_VARIANTE_REPLI|BATTERYCURRENT_MAX|VOLTAGE_MIN|REVERSE|CURRENT_SIGN) (\S+)", macros, re.M)}
print("macros :", mv)
if ROUTE:
    attendu_r = {"PH_CURRENT_TRIP": "245", "PH_CURRENT_MAX": "98", "SPEEDLIMIT": "1500", "ROUTE_NIVEAU_MAX": "9",
                 "BATTERYCURRENT_MAX": "18000", "U_MAX_BENCH": "1800L", "TRIGGER_OFFSET_ADC_DYN": "65", "VOLTAGE_MIN": "1773", "REVERSE": "1", "CURRENT_SIGN": "1",
                 "ROUTE_VARIANTE_REPLI": "2"}
    ecarts = {k: (mv.get(k), v) for k, v in attendu_r.items() if mv.get(k) != v}
    print("seuils route lot B3 (trip de harnais 245 / 98 / 15 km/h / niveau 5 / 18 A / U_MAX 1800 / avance dyn 65 / 30 V / REVERSE +1 / CURRENT_SIGN +1) :",
          "OK" if not ecarts else f"A VERIFIER {ecarts}")
else:
    print("banc : pas de repli de variante (ROUTE_VARIANTE_REPLI absente) :", "OK" if "ROUTE_VARIANTE_REPLI" not in mv else "A VERIFIER")
    print("seuils de banc 45 / 20 / 15 :", "OK" if (mv.get("PH_CURRENT_TRIP"), mv.get("PH_CURRENT_MAX"), mv.get("BENCH_WALK_ILIM_MAX")) == ("45", "20", "15") else "A VERIFIER")
nom_us = "update_setpoint" if "update_setpoint" in syms else "main"
a_us, t_us = syms[nom_us][0], syms[nom_us][1]
us = run("arm-none-eabi-objdump", "-d", "--no-show-raw-insn", f"--start-address={a_us:#x}", f"--stop-address={a_us + t_us:#x}", ELF)
if ROUTE:
    print(f"route : {nom_us} n'utilise plus le walk par CAN (bench_walk non lu) :", "OK" if not lit_adresse(us, "bench_walk") else "A VERIFIER")
    print(f"route : {nom_us} lit le niveau d'assistance et la consigne de poussee (MS) :", "OK" if lit_adresse(us, "MS") else "A VERIFIER")
else:
    print(f"bride erps : {nom_us} lit ui16_erps et ui16_timertics :", "OK" if lit_adresse(us, "ui16_erps") and lit_adresse(us, "ui16_timertics") else "A VERIFIER")
def fonctions_lisant(sym):
    """noms des fonctions dont le pool litteral contient l'adresse de sym"""
    return [m.group(1) for m in re.finditer(r"^[0-9a-f]+ <([^>]+)>:\n(.*?)(?=\n\n)", dis, re.S | re.M)
            if lit_adresse(m.group(2), sym)]


def gpio_istat(corps, base, base_pool=None):
    """lecture du registre d'entree (decalage 8) d'un port GPIO : adresse en clair, ou une base du pool litteral (base_pool,
    par defaut le port lui-meme) puis le decalage ; le compilateur adresse GPIOD (0x40011400) depuis GPIOC : #1032 = 0x408"""
    bp = base if base_pool is None else base_pool
    return bool(re.search(rf"\.word\s+0x{base + 8:08x}\b", corps)
                or (re.search(rf"\.word\s+0x{bp:08x}\b", corps) and re.search(rf"#{base + 8 - bp}\]", corps)))


if ROUTE:
    nom_ra = "reg_ADC_processing" if "reg_ADC_processing" in syms else "main"
    a_ra, t_ra = syms[nom_ra][0], syms[nom_ra][1]
    ra = run("arm-none-eabi-objdump", "-d", "--no-show-raw-insn", f"--start-address={a_ra:#x}",
             f"--stop-address={a_ra + t_ra:#x}", ELF)
    print(f"pedalier (27/10) : {nom_ra} lit GPIOC_ISTAT (0x40011008) et GPIOD_ISTAT (0x40011408), ecrit pedalier_diag :",
          "OK" if gpio_istat(ra, 0x40011000) and (gpio_istat(ra, 0x40011400) or gpio_istat(ra, 0x40011400, 0x40011000)) and lit_adresse(ra, "pedalier_diag") else "A VERIFIER")
    a_ex, t_ex = syms["EXTI10_15_IRQHandler"][0], syms["EXTI10_15_IRQHandler"][1]
    ex = run("arm-none-eabi-objdump", "-d", "--no-show-raw-insn", f"--start-address={a_ex:#x}",
             f"--stop-address={a_ex + t_ex:#x}", ELF)
    print("pedalier : EXTI10_15_IRQHandler compte pedalier_exti12 et ne leve plus PAS_flag (cadence sur PD2) :",
          "OK" if lit_adresse(ex, "pedalier_exti12") and not lit_adresse(ex, "PAS_flag") else "A VERIFIER")
    print(f"cadence PD2 (27/10) : {nom_ra} ecrit pd2_ar et PAS_flag :",
          "OK" if lit_adresse(ra, "pd2_ar") and lit_adresse(ra, "PAS_flag") else "A VERIFIER")
    lecteurs_j = fonctions_lisant("pd2_journal")
    print(f"journal des periodes PD2 (E210) : ecrit par {nom_ra}, lu par {lecteurs_j} :",
          "OK" if lit_adresse(ra, "pd2_journal") and "bench_can_poll" in lecteurs_j else "A VERIFIER")
    print("frequence reg_ADC (27/10) : TIMER1_IRQHandler compte freq_n_isr, reg_ADC_processing freq_n_trait, main freq_n_boucle :",
          "OK" if "TIMER1_IRQHandler" in fonctions_lisant("freq_n_isr") and nom_ra in fonctions_lisant("freq_n_trait")
          and "main" in fonctions_lisant("freq_n_boucle") else "A VERIFIER")
    a_st, t_st = syms["SysTick_Handler"][0], syms["SysTick_Handler"][1]
    st = run("arm-none-eabi-objdump", "-d", "--no-show-raw-insn", f"--start-address={a_st:#x}",
             f"--stop-address={a_st + t_st:#x}", ELF)
    print(f"frequence reg_ADC : SysTick_Handler fige les fenetres (route_freq_ms ou freq_diag) ; E212 lue par {fonctions_lisant('freq_diag')} :",
          "OK" if ("route_freq_ms" in st or lit_adresse(st, "freq_diag")) and "bench_can_poll" in fonctions_lisant("freq_diag")
          else "A VERIFIER")
    lec_t = fonctions_lisant("route_ticks_sans_hall")
    print(f"MOE en rotation (27/10) : route_ticks_sans_hall ecrit par TIMER1/TIMER2, lu par {nom_us} ({lec_t}) :",
          "OK" if {"TIMER1_IRQHandler", "TIMER2_IRQHandler", nom_us} <= set(lec_t) else "A VERIFIER")
    print(f"MOE en rotation : {nom_ra} lit ui_8_PWM_ON_Flag (RAZ du PI pont coupe seulement) :",
          "OK" if lit_adresse(ra, "ui_8_PWM_ON_Flag") else "A VERIFIER")
    lec_4k = fonctions_lisant("route_ticks_4k")
    print(f"base de temps (lot A, 28/10) : route_ticks_4k ecrit par TIMER1_IRQHandler, lu par {nom_ra} avec route_bt_prec ({lec_4k}) :",
          "OK" if {"TIMER1_IRQHandler", nom_ra} <= set(lec_4k) and lit_adresse(ra, "route_bt_prec") else "A VERIFIER")
    lec_r = fonctions_lisant("route_rampe_debut")
    print(f"rampe au reveil de MOE (lot A) : route_rampe_debut ecrit au MOE = 1, lu par {nom_us} avec route_rampe_active ({lec_r}) :",
          "OK" if nom_us in lec_r and len(lec_r) >= 2 and lit_adresse(us, "route_rampe_active") else "A VERIFIER")
    lec_n = fonctions_lisant("route_niveau_demande")
    print(f"socle (28/10) : route_niveau_demande ecrit par processCAN_Rx, lu par main ({lec_n}) ; route_l5 lu par main "
          f"seulement si le timer L5 est compile (30/10 : retire) :",
          "OK" if {"processCAN_Rx", "main"} <= set(lec_n) and (("main" in fonctions_lisant("route_l5")) == timer_l5) else "A VERIFIER")
    lecteurs = fonctions_lisant("pedalier_exti12")
    print(f"pedalier : trame E20F, compteurs lus hors interruption par {lecteurs} :",
          "OK" if any(f != "EXTI10_15_IRQHandler" for f in lecteurs) and fonctions_lisant("pedalier_diag") else "A VERIFIER")
else:
    print("banc : regle de la porte 3 conservee (route_ticks_sans_hall absent) :",
          "OK" if "route_ticks_sans_hall" not in syms else "A VERIFIER")
    print("banc : ni base de temps ni rampe du lot A (route_ticks_4k, route_bt_prec, route_rampe_debut absents) :",
          "OK" if not any(x in syms for x in ("route_ticks_4k", "route_bt_prec", "route_rampe_debut")) else "A VERIFIER")
    print("banc : pas de mesure de frequence (freq_diag, freq_n_isr, route_freq_ms absents) :",
          "OK" if not any(x in syms for x in ("freq_diag", "freq_n_isr", "route_freq_ms")) else "A VERIFIER")
    print("banc : pas de diagnostic du pedalier (pedalier_diag, pedalier_exti12, pd2_journal absents) :",
          "OK" if not any(x in syms for x in ("pedalier_diag", "pedalier_exti12", "pd2_journal")) else "A VERIFIER")
    a_ex, t_ex = syms["EXTI10_15_IRQHandler"][0], syms["EXTI10_15_IRQHandler"][1]
    ex = run("arm-none-eabi-objdump", "-d", "--no-show-raw-insn", f"--start-address={a_ex:#x}",
             f"--stop-address={a_ex + t_ex:#x}", ELF)
    print("banc : EXTI10_15_IRQHandler leve PAS_flag (PC12, inchange) :", "OK" if lit_adresse(ex, "PAS_flag") else "A VERIFIER")
if "PAS_processing" in syms:
    a_pp, t_pp = syms["PAS_processing"][0], syms["PAS_processing"][1]
    pp = run("arm-none-eabi-objdump", "-d", "--no-show-raw-insn", f"--start-address={a_pp:#x}",
             f"--stop-address={a_pp + t_pp:#x}", ELF)
    if ROUTE:
        print("cadence PD2 : PAS_processing seuil 20 ticks, 5000 / periode, sans lecture de sens (gpio_input_bit_get) :",
              "OK" if re.search(r"cmp\s+r\d+, #20\b", pp) and "#5000" in pp and "gpio_input_bit_get" not in pp
              else "A VERIFIER")
        print("zero de couple adaptatif (27/10) : PAS_processing ecrit couple_zero, plus de seuil fixe 750 :",
              "OK" if lit_adresse(pp, "couple_zero") and not re.search(r"#750\b", pp) else "A VERIFIER")
        print(f"zero de couple : {nom_ra} lit couple_zero (relance du delai d'arret) ; E211 lue par {fonctions_lisant('couple_zero')} :",
              "OK" if lit_adresse(ra, "couple_zero") and "bench_can_poll" in fonctions_lisant("couple_zero") else "A VERIFIER")
    else:
        print("banc : PAS_processing d'origine (seuil 70, 10000 / periode, sens lu sur PD2, zero fixe 750) :",
              "OK" if re.search(r"cmp\s+r\d+, #70\b", pp) and "#10000" in pp and "gpio_input_bit_get" in pp
              and re.search(r"#750\b", pp) and "couple_zero" not in syms else "A VERIFIER")
else:
    print("PAS_processing absent (inline) : A VERIFIER")
print("regle F4 des entrees du trip : prouvee par le test hote tools/test_f4_mode_commun.c (en-tete reel)")

section("Afficheur (29/10) : trames d'etat (C2), extinction propre (C1), releve E213 (C3), route seulement")
aff_syms = ("route_ext", "route_aff_rx", "route_aff_cal", "sendCAN_Etat")
print("   symboles :", {x: ("present" if x in syms else "absent") for x in aff_syms})
table_codes = struct.pack("<7H", 0x3202, 0x3206, 0x3207, 0x3204, 0x1200, 0x3000, 0x3204)   # codes de aff_id()
n_table, n_e213 = img.count(table_codes), img.count(bytes([0xE2, 0x13, 0xE8, 0x03]))      # E213 a 1000 ms (trames[])
print(f"   table des codes 3202/3206/3207/3204/1200/3000 dans l'image : x{n_table} ; E213 dans la table de telemetrie : x{n_e213}")


def ecritures(corps, decal):
    """valeurs ecrites (movs #V juste avant) par les str [rX, #decal] du corps, dans l'ordre"""
    l, vals = corps.splitlines(), []
    for k, ligne in enumerate(l):
        m = re.search(rf"\bstr(?:\.w)?\s+(r\d+), \[r\d+, #{decal}\]", ligne)
        if m:
            v = None
            for j in range(k - 1, max(k - 7, -1), -1):          # registre remonte jusqu'a sa derniere ecriture
                mv = re.search(rf"\bmovs?(?:\.w)?\s+{m.group(1)}, #(\d+)", l[j])
                if mv:
                    v = int(mv.group(1))
                    break
                if re.search(rf":\s+(?!str)\S+\s+{m.group(1)}\b", l[j]):   # ecrit autrement : valeur inconnue
                    break
            vals.append(v)
    return vals


def contient(vals, motif):
    return any(vals[i:i + len(motif)] == motif for i in range(len(vals) - len(motif) + 1))


bc, bop = ecritures(dis_main, 3092), ecritures(dis_main, 3088)          # GPIOB_BC 0x40010c14, GPIOB_BOP 0x40010c10
print(f"   main : GPIOB_BC (base 0x40010000 + 0xc14) <- {bc} ; GPIOB_BOP (+ 0xc10) <- {bop}")
lec_shut = fonctions_lisant("shutoffcounter") if "shutoffcounter" in syms else []
print(f"   shutoffcounter (extinction d'EBiCS) lu par : {lec_shut}")
if ROUTE:
    lec_e, lec_rx, lec_cal = fonctions_lisant("route_ext"), fonctions_lisant("route_aff_rx"), fonctions_lisant("route_aff_cal")
    print(f"   route_ext lu par {lec_e} ; route_aff_rx par {lec_rx} ; route_aff_cal par {lec_cal}")
    se = func("sendCAN_Etat", 0) if "sendCAN_Etat" in syms else ""
    print("route : sendCAN_Etat appelee par main, lit TSTAT de CAN0 (0x40006400) et route_aff_rx (echo, 0x6300 vue) :",
          "OK" if "<sendCAN_Etat>" in dis_main and re.search(r"\.word\s+0x40006400\b", se) and lit_adresse(se, "route_aff_rx")
          else "A VERIFIER")
    print("route : table des codes des trames d'etat presente une fois, E213 dans la telemetrie :",
          "OK" if n_table == 1 and n_e213 == 1 else "A VERIFIER")
    print("route : processCAN_Rx releve les trames de l'afficheur, E213 lue par bench_can_poll :",
          "OK" if {"processCAN_Rx", "sendCAN_Etat", "bench_can_poll"} <= set(lec_rx) else "A VERIFIER")
    print("route : verrou de l'extinction lu par pwm_enable_safe, main (boucle lente) et bench_can_poll (E213) :",
          "OK" if {"pwm_enable_safe", "main", "bench_can_poll"} <= set(lec_e) else "A VERIFIER")
    print("route : coupure PB5, PB4, PB3 dans cet ordre (BC 32, 16, 8) et rallumage PB4, PB3, PB5 (BOP 16, 8, 32) :",
          "OK" if contient(bc, [32, 16, 8]) and contient(bop, [16, 8, 32]) else "A VERIFIER")
    print("route : extinction d'EBiCS retiree (shutoffcounter non lu par main) :", "OK" if "main" not in lec_shut else "A VERIFIER")
else:
    print("banc : pas de trames d'etat ni d'E213 (symboles, table des codes et entree E213 absents) :",
          "OK" if not any(x in syms for x in aff_syms) and n_table == 0 and n_e213 == 0 else "A VERIFIER")
    print("banc : extinction d'EBiCS inchangee (shutoffcounter lu par main, PB3 jamais coupe) :",
          "OK" if "main" in lec_shut and 8 not in bc else "A VERIFIER")

section("Table de vecteurs (debut de ebics.bin) : entrees qui tombent sur Default_Handler")
names = []
for l in open("port_f10x/startup_gd32f10x_hd.S", encoding="ascii").read().split("__gVectors:")[1].splitlines():
    m = re.match(r"\s*\.word\s+(\w+)", l)
    if m:
        names.append(m.group(1))
binary = open(BIN, "rb").read()
dh = syms["Default_Handler"][0] | 1 if "Default_Handler" in syms else None
if dh is None:
    dh = int(re.search(r"^([0-9a-f]+) <Default_Handler>:", dis, re.M).group(1), 16) | 1
vec = struct.unpack_from(f"<{len(names)}I", binary, 0)
on_default = [(i, n) for i, (n, v) in enumerate(zip(names, vec)) if v == dh]
print(f"{len(names)} entrees, Default_Handler = {dh:#010x}, {len(on_default)} entrees sur Default_Handler")
enabled = set()
for f in ("src/main.c", "src/gd32f307c_eval.c"):
    for m in re.finditer(r"^\s*nvic_irq_enable\((\w+)", open(f, encoding="utf-8", errors="replace").read(), re.M):
        enabled.add(m.group(1))
print("IRQ activees dans le code (nvic_irq_enable non commente) :", ", ".join(sorted(enabled)))
handler_of = {n.replace("_IRQHandler", "_IRQn"): (i, n, v) for i, (n, v) in enumerate(zip(names, vec))}
bad = []
for irq in sorted(enabled):
    i, n, v = handler_of.get(irq, (None, None, None))
    etat = "handler propre" if v not in (None, dh) else ("SUR Default_Handler" if v == dh else "nom non resolu")
    if v == dh:
        bad.append(irq)
    print(f"   {irq:22s} -> {n} @ {v:#010x} : {etat}" if v is not None else f"   {irq:22s} -> {etat}")
print("BILAN : aucune IRQ activee ne tombe sur Default_Handler" if not bad else f"BILAN : IRQ activee(s) sans handler : {bad}")
print("Exceptions systeme (0-15) avec handler propre :",
      ", ".join(n for i, (n, v) in enumerate(zip(names[:16], vec[:16])) if i >= 2 and v not in (0, dh)))

section("Usage de pile (.su, octets par fonction, statique)")
su = {}
for root, _, files in os.walk(f"{B}/src"):
    for f in files:
        if f.endswith(".su"):
            for l in open(os.path.join(root, f)):
                p = l.rstrip("\n").split("\t")
                su[p[0].split(":")[-1]] = (int(p[1]), p[2])
for n, (s, k) in sorted(su.items(), key=lambda x: -x[1][0])[:12]:
    print(f"   {s:4d}  {k:8s} {n}")
chain = ["ADC0_1_IRQHandler", "FOC_calculation", "runPIcontrol", "PI_control", "safety_check_phase_currents", "dyn_adc_state",
         "bench_ol_vector", "svpwm"]
print("Chaine de l'ISR ADC (pire cas = ISR + FOC_calculation + runPIcontrol + PI_control, plus les fonctions CMSIS feuilles) :")
for n in chain:
    print(f"   {n:30s} {su.get(n, ('?', ''))[0]}")
print("Pile reservee : 2048 octets (aoci_flash.ld). Fonctions CMSIS (arm_*) compilees sans .su : feuilles, quelques octets.")
