/*
 * Test hote du plafond de l'integrale du PI de i_q (inc/pi_control.h, coeur reel de PI_control), 29/10/2026.
 *   gcc -std=gnu99 -Wall -Wextra -Iinc tools/test_pi_plafond_integrale.c -o test_pi_plafond.exe
 * Meme modele que tools/test_pi_antiwindup.c : charge R-L du premier ordre (L/R = 25 periodes, R = 3 ticks par point),
 * mesure filtree IIR 1/8 comme MS.i_q, gains du firmware (P 1,5, I 0,01, max_step 15, sortie <= 1500). Force
 * contre-electromotrice E constante (moteur a vitesse fixee par le pedalier), consigne 14 pts (B1.2 du 29/10).
 * Plafond 1125 (3/4, avant) contre 1500 (_U_MAX, build route).
 * Attendu : validation/porte-4/correctif-plafond-integrale-iq-attendu-20261029.md.
 */
#include <stdio.h>
#include "pi_control.h"

#define Q16(x) ((int32_t)((x) * 65536.0))
static int n_ok = 0, n_ko = 0;
#define CHECK(cond, ...) do { if (cond) { n_ok++; } else { n_ko++; printf("  ECHEC : "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const int32_t GP = Q16(1.5), GI = Q16(0.01), UMAX = 1500, MAXSTEP = 15;

typedef struct { double i; int32_t u; int32_t integ_max; } resultat_t;

/* regime etabli a E constant : n periodes, renvoie le courant et la sortie finale, et le maximum de l'integrale */
static resultat_t regime(int32_t limi, double E, int32_t consigne, int n)
{
    int32_t integ = 0, out = 0, imax = 0;
    double i = 0.0, ifil = 0.0;
    for (int k = 0; k < n; k++) {
        int32_t u = pi_pas(consigne, (int32_t)(ifil >= 0 ? ifil + 0.5 : ifil - 0.5), GP, GI, limi, UMAX, MAXSTEP, &integ, &out);
        i += ((u - E) / 3.0 - i) / 25.0;
        ifil += (i - ifil) / 8.0;
        if (integ > imax) imax = integ;
    }
    resultat_t r = {i, out, imax};
    return r;
}

/* scenario « 10 pts sous charge » du 27/10 (E monte a 290 puis chute vers 30) : courant max apres la chute */
static double dix_points(int32_t limi)
{
    int32_t integ = 0, out = 0;
    double i = 0.0, ifil = 0.0, imax = 0.0;
    for (int k = 0; k < 6000; k++) {
        double E = k < 3000 ? 290.0 * k / 3000 : 290.0 - 260.0 * (k - 3000) / 300.0;
        if (E < 30.0) E = 30.0;
        int32_t u = pi_pas(10, (int32_t)(ifil >= 0 ? ifil + 0.5 : ifil - 0.5), GP, GI, limi, UMAX, MAXSTEP, &integ, &out);
        i += ((u - E) / 3.0 - i) / 25.0;
        ifil += (i - ifil) / 8.0;
        if (k >= 3000 && i > imax) imax = i;
    }
    return imax;
}

int main(void)
{
    const int N = 200000;                                  /* ~12 s a 16,6 kHz : regime etabli */
    resultat_t a = regime(1125, 1150.0, 14, N), b = regime(1500, 1150.0, 14, N);
    printf("E 1150 (erps ~230) : plafond 1125 -> i %.1f u %d ; plafond 1500 -> i %.1f u %d\n", a.i, (int)a.u, b.i, (int)b.u);
    CHECK(a.i <= 2.0, "plafond 1125 : i_q bride (%.1f), comme au terrain", a.i);
    CHECK(b.i >= 13.0 && b.i <= 15.0, "plafond 1500 : i_q suit la consigne (%.1f)", b.i);
    CHECK(b.u >= 1185 && b.u <= 1200, "plafond 1500 : u_q ~1192 (%d)", (int)b.u);

    resultat_t c = regime(1500, 1400.0, 14, N);
    printf("E 1400 (erps ~280) : plafond 1500 -> i %.1f u %d\n", c.i, (int)c.u);
    CHECK(c.i >= 13.0 && c.i <= 15.0, "E 1400 : i_q suit (%.1f)", c.i);

    resultat_t d = regime(1500, 1480.0, 14, N);
    printf("E 1480 : plafond 1500 -> i %.1f u %d integrale max %.1f\n", d.i, (int)d.u, d.integ_max / 65536.0);
    CHECK(d.u == UMAX, "E 1480 : sortie saturee a 1500 (%d)", (int)d.u);
    CHECK(d.i >= 6.0 && d.i <= 7.5, "E 1480 : i_q limite par la tension ~6,7 (%.1f)", d.i);
    CHECK(d.integ_max <= (1500 << 16), "E 1480 : integrale bornee a 1500");

    resultat_t e = regime(1500, 1500.0, 14, N);
    printf("E 1500 : plafond 1500 -> i %.1f u %d integrale max %.1f\n", e.i, (int)e.u, e.integ_max / 65536.0);
    CHECK(e.u == UMAX && e.i > -1.0 && e.i < 1.0, "E 1500 : sortie 1500, courant nul");
    CHECK(e.integ_max <= (1500 << 16), "E 1500 : pas d'emballement de l'integrale");

    double p1125 = dix_points(1125), p1500 = dix_points(1500);
    printf("scenario 10 pts sous charge : courant max apres la chute %.1f (1125) / %.1f (1500)\n", p1125, p1500);
    CHECK(p1500 - p1125 <= 1.0 && p1125 - p1500 <= 1.0, "scenario 10 pts identique a 1 pt pres");

    printf("== %d verifications OK, %d ECHECS -> %s\n", n_ok, n_ko, n_ko ? "ROUGE" : "VERT");
    return n_ko ? 1 : 0;
}
