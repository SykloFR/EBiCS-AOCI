"""Preuves du build de banc porte 4 (A1, revue Fangyan point 13). Lecture seule : n'ecrit que le rapport.
Usage depuis la racine du depot, apres  mingw32-make OS= CORE=m3 BENCH=1 CURRENT_SIGN=-1 aoci :
    python -I tools/bench/a1_preuves.py build-m3-banc > build-m3-banc/preuves_a1.txt
Contenu : empreintes, size, macros de banc effectives, symboles banc, desassemblage des points de securite,
table de vecteurs (entrees sur Default_Handler croisees avec les IRQ activees), usage de pile (.su)."""
import hashlib, os, re, struct, subprocess, sys

B = sys.argv[1] if len(sys.argv) > 1 else "build-m3-banc"
ELF, BIN = f"{B}/ebics.elf", f"{B}/ebics.bin"
IMG = f"{B}/ebics_BANC_aoci_full.bin"


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
          "-DHXTAL_VALUE=12000000U", "-DBENCH_BUILD", "-DCURRENT_SIGN=-1", "-Iport_f10x", "-Iinc", "-IFirmware_F10x/CMSIS",
          "-IFirmware_F10x/CMSIS/GD/GD32F10x/Include", "-IFirmware_F10x/GD32F10x_standard_peripheral/Include"]
macros = run("arm-none-eabi-gcc", *cflags, "-dM", "-E", "src/main.c")
want = r"^#define (BENCH_\w+|PH_CURRENT_MAX|PH_CURRENT_TRIP|PH_TRIP_PERIODS|BATTERYCURRENT_MAX|VOLTAGE_MIN|" \
       r"TEMP_SENSOR_REQUIRED|U_MAX_BENCH|_U_MAX|CURRENT_SIGN|_T|DYNAMIC_ADC_THRESHOLD|REVERSE|LEGALFLAG|GEAR_RATIO|" \
       r"CAL_PONT_\w+|VARIANTE_ADDR|PH_COUNTS_PER_A_\w+|TRIGGER_DEFAULT)\b.*"
for line in sorted(l for l in macros.splitlines() if re.match(want, l)):
    print(line)

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
if "offset_cal_pont_50" in syms:
    print("calibrate_current_offsets appelle offset_cal_pont_50 :", "OK" if "<offset_cal_pont_50>" in cal else "A VERIFIER")
else:
    print("offset_cal_pont_50 integree (inline) : garde lue dans calibrate_current_offsets :",
          "OK" if lit_adresse(cal, "offset_cal_pont") else "A VERIFIER")

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
