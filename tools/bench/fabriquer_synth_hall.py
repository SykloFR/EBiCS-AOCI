"""Fabrique un journal synthetique par classe (synth_a..e.csv) AU FORMAT EXACT de t22 (meme en-tete, memes arrondis).
Emule ce que fait le firmware 713f4dd, pas un ideal :
  - TIMER2 relance a chaque front, periode 0xFFFF : duree capturee = ecart reel modulo 65536 tics (2 us) ;
  - switch EBiCS sans « default » : cas hors des 12 -> angle et sens du front precedent (perimes) ;
  - ui8_hall_state_old mis a jour a chaque front, meme pour un etat 0 ou 7 ;
  - drapeau 6-step vu au front = celui decide par l'ISR ADC avant le front (ecart > 40 ms -> 1 ; apres un front :
    capture < 10000 -> 0, > 15000 -> 1, sinon inchange) ;
  - angle journal = (uint32)q31_rotorposition_hall >> 24 ; journal circulaire de 64, rang 0 = le plus ancien garde.
Bibliotheque standard. Usage : python fabriquer_synth.py [dossier_sortie]"""
import csv, os, random, sys

Q31 = {64: -411601033, 45: 238609294, 51: 948471945, 13: 1712021686, 32: -1837291566, 26: -1109533218}
ORDER = -1                                            # BENCH_HALL_ORDER
AVANT = {64, 45, 51, 13, 32, 26}
ARRIERE = {46, 62, 23, 31, 15, 54}
ARRET = {2: 32, 6: 26, 4: 64, 5: 45, 1: 51, 3: 13}    # get_standstill_position()


class Emul:
    def __init__(self, etat0):
        self.old, self.q31 = etat0, Q31[ARRET[etat0]]
        self.dir, self.flag6, self.journal = 0, 1, []

    def front(self, etat, ecart_tics):
        tics = int(ecart_tics) % 65536 or 1
        if ecart_tics > 20000:
            self.flag6 = 1                            # ISR ADC : compteur > 2 * SIXSTEPTHRESHOLD
        cas = self.old * 10 + etat
        self.old = etat
        if cas in AVANT:
            self.q31, self.dir = Q31[cas], -ORDER
        elif cas in ARRIERE:
            self.q31, self.dir = Q31[(cas % 10) * 10 + cas // 10], ORDER
        fl = self.flag6
        self.journal.append(dict(cas=cas, tics=tics, six_step=fl, sens=self.dir,
                                 angle_u8=(self.q31 % (1 << 32)) >> 24))
        if tics < 10000:
            self.flag6 = 0
        elif tics > 15000:
            self.flag6 = 1


def ecrire(chemin, journal):
    garde = journal[-64:]
    with open(chemin, "w", newline="", encoding="utf-8") as f:   # copie de t22_journal_fronts_hall.py:58-63
        w = csv.writer(f)
        w.writerow(["rang", "cas", "transition", "sens_sequence", "duree_ms", "six_step", "sens_detecte", "angle_deg"])
        for rang, v in enumerate(garde):
            c = v["cas"]
            sq = "avant" if c in AVANT else ("arriere" if c in ARRIERE else "hors sequence")
            w.writerow([rang, c, f"{c // 10}->{c % 10}", sq, round(v["tics"] * 0.002, 3), v["six_step"], v["sens"],
                        round(v["angle_u8"] * 360 / 256, 1)])
    return len(garde)


def ms(x):
    return x * 500.0                                   # ms -> tics de 2 us


def synth_a(rnd):
    """rotor assis sur la frontiere 1|3 : 13, 31, 13, ... ecarts 2-9 ms ; 1er front apres 300 ms (capture modulo)."""
    e = Emul(1)
    e.front(3, ms(300))
    etat = 3
    for _ in range(21):
        etat = 1 if etat == 3 else 3
        e.front(etat, ms(rnd.uniform(2, 9)))
    return e.journal


def synth_b(rnd):
    """rotation arriere qui accelere : 4-6-2-3-1-5-4 ..., ecarts 30 ms -> 4 ms."""
    e = Emul(4)
    seq = [6, 2, 3, 1, 5, 4]
    for k in range(30):
        e.front(seq[k % 6], ms(30 - 26 * k / 29 + rnd.uniform(-0.3, 0.3)))
    return e.journal


def synth_c(rnd):
    """etat 3 a l'arret, parasites de commutation : 3->7->3, 3->3, sauts non adjacents, durees 6-60 us."""
    e = Emul(3)
    e.front(1, ms(250))
    etat = 1
    for k in range(24):
        if k % 3 == 0:
            e.front(7, ms(rnd.uniform(0.006, 0.06))); e.front(etat, ms(rnd.uniform(0.006, 0.06)))
        elif k % 3 == 1:
            e.front(etat, ms(rnd.uniform(0.006, 0.04)))           # etat relu identique (rebond) : cas xx
        else:
            nouveau = 3 if etat == 1 else 1
            e.front(nouveau, ms(rnd.uniform(1, 6))); etat = nouveau
            if k % 6 == 5:
                e.front(4, ms(rnd.uniform(0.01, 0.05))); e.front(etat, ms(rnd.uniform(0.01, 0.05)))   # non adjacent
    return e.journal


def synth_d(rnd):
    """rotation avant reguliere, erps vrai = 95 (t - 0,08 s) comme l'ajustement M2 des traces F6, jusqu'a 1,8 s ;
    le journal ne garde que les 64 derniers fronts."""
    e = Emul(6)
    seq = [4, 5, 1, 3, 2, 6]
    t, phase, dt, t_der, k = 0.08, 0.0, 1e-5, 0.0, 0
    while t < 1.8:
        t += dt
        phase += 6 * 95 * (t - 0.08) * dt
        if phase >= 1.0:
            phase -= 1.0
            e.front(seq[k % 6], ms((t - t_der) * 1000) * rnd.uniform(0.97, 1.03))
            t_der, k = t, k + 1
    return e.journal


def synth_e(rnd):
    return []


if __name__ == "__main__":
    dossier = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
    rnd = random.Random(20261027)
    for nom, f in [("a", synth_a), ("b", synth_b), ("c", synth_c), ("d", synth_d), ("e", synth_e)]:
        j = f(rnd)
        n = ecrire(os.path.join(dossier, f"synth_{nom}.csv"), j)
        print(f"synth_{nom}.csv : {len(j)} fronts emules, {n} dans le journal")
