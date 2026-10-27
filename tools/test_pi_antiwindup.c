/*
 * Test hote de l'anti-windup du PI de courant (inc/pi_control.h, le coeur reel de PI_control, src/FOC.c).
 *   gcc -std=gnu99 -Wall -Wextra -Iinc tools/test_pi_antiwindup.c -o test_pi.exe
 *   gcc -std=gnu99 -Wall -Wextra -Iinc -DPI_SANS_ANTIWINDUP tools/test_pi_antiwindup.c -o test_pi_avant.exe
 * Gains du firmware (config.h) : P 1,5, I 0,01 (Q16), max_step 15, limite de sortie _U_MAX (1500 au banc).
 * 1. Proprietes : integration bloquee quand la sortie est limitee dans le sens de l'erreur, libre sinon ; plafond 3/4.
 * 2. Scenario « 10 pts sous charge » (seance echelle du 27/10) : charge R-L du premier ordre, L/R = 25 periodes PWM,
 *    R = 3 ticks par point (30 ticks pour 10 pts, revue du 27/10), force contre-electromotrice E qui monte pendant
 *    l'acceleration puis chute (moteur qui cale), mesure filtree IIR 1/8 comme MS.i_q (FOC.c). On compare la sortie et le
 *    courant avec et sans anti-windup. Modele simplifie : il ne pretend pas reproduire le moteur, il montre si le
 *    correctif agit dans ce cas de figure.
 */
#include <stdio.h>
#include <stdlib.h>
#include "pi_control.h"

#define Q16(x) ((int32_t)((x) * 65536.0))
static int n_ok = 0, n_ko = 0;
#define CHECK(cond, ...) do { if (cond) { n_ok++; } else { n_ko++; printf("  ECHEC : "); printf(__VA_ARGS__); printf("\n"); } } while (0)

typedef struct { int32_t integ, out; } pi_t;
static const int32_t GP = Q16(1.5), GI = Q16(0.01), UMAX = 1500, MAXSTEP = 15;
static const int32_t LIMI = (1500 * PI_LIMITE_I_NUM) / PI_LIMITE_I_DEN;

static int32_t pas(pi_t *p, int32_t sp, int32_t mes) { return pi_pas(sp, mes, GP, GI, LIMI, UMAX, MAXSTEP, &p->integ, &p->out); }

/* scenario : renvoie la sortie et le courant aux instants cles ; E(k) en ticks */
static void scenario(int afficher, int *u_fin, int *periodes_100_30, double *i_max_apres, int *periodes_i_sous_20)
{
    pi_t p = {0, 0};
    double i = 0.0, ifil = 0.0;
    const double R = 3.0, tau = 25.0;
    int k_chute = 3000, t30 = -1, t20 = -1, k_100 = -1;
    double imax = 0.0;
    for (int k = 0; k < 6000; k++) {
        double E;
        if (k < k_chute) E = 290.0 * k / k_chute;                 /* acceleration : E monte jusqu'a 290 ticks */
        else E = 290.0 - 260.0 * (k - k_chute) / 300.0;           /* chute rapide vers 30 en 300 periodes (~18 ms) */
        if (E < 30.0) E = 30.0;
        int32_t u = pas(&p, 10, (int32_t)(ifil >= 0 ? ifil + 0.5 : ifil - 0.5));
        i += ((u - E) / R - i) / tau;                             /* R-L premier ordre, en points de courant */
        ifil += (i - ifil) / 8.0;                                 /* filtre IIR 1/8 de MS.i_q */
        if (k >= k_chute) {
            if (i > imax) imax = i;
            if (k_100 < 0 && u <= 100) k_100 = k;
            if (k_100 >= 0 && t30 < 0 && u <= 30) t30 = k - k_100;
            if (t20 < 0 && i > 20.0) t20 = -2;                    /* marque : a depasse 20 */
            if (t20 == -2 && i < 20.0) t20 = k - k_chute;
        }
        if (afficher && (k == k_chute - 1 || (k >= k_chute && (k - k_chute) % 50 == 0 && k < k_chute + 400)))
            printf("    k %5d  E %6.1f  u %5d  integrale %7.1f  i %6.1f\n", k, E, (int)u, p.integ / 65536.0, i);
    }
    *u_fin = (int)p.out; *periodes_100_30 = t30; *i_max_apres = imax; *periodes_i_sous_20 = t20;
}

int main(void)
{
#ifdef PI_SANS_ANTIWINDUP
    printf("== PI : comportement ANTERIEUR (sans anti-windup), limite d'integrale %d\n", (int)LIMI);
#else
    printf("== PI : anti-windup (integration conditionnelle, integrale <= %d/%d de la sortie = %d)\n",
           PI_LIMITE_I_NUM, PI_LIMITE_I_DEN, (int)LIMI);
#endif
    /* 1a. sortie en butee haute, erreur positive : l'integrale ne doit pas avancer */
    pi_t p = {0, UMAX};
    int32_t i0;
    for (int k = 0; k < 50; k++) pas(&p, 1000, 0);            /* erreur enorme, sortie bloquee a UMAX */
    i0 = p.integ;
    for (int k = 0; k < 100; k++) pas(&p, 1000, 0);
    printf("[1] butee haute, erreur > 0 : integrale %.1f -> %.1f\n", i0 / 65536.0, p.integ / 65536.0);
#ifndef PI_SANS_ANTIWINDUP
    CHECK(p.integ == i0, "butee haute : l'integrale a avance");
#endif
    /* 1b. butee haute, erreur negative : l'integrale doit descendre */
    i0 = p.integ;
    for (int k = 0; k < 10; k++) pas(&p, 0, 1000);
    CHECK(p.integ < i0 || i0 <= 0, "butee haute, erreur < 0 : l'integrale ne descend pas");
    /* 1c. plafond de l'integrale */
    pi_t q = {0, 0};
    for (int k = 0; k < 100000; k++) pas(&q, 5, 0);
    printf("[1c] integrale apres 100000 periodes d'erreur +5 sans butee : %.1f (plafond %d)\n", q.integ / 65536.0, (int)LIMI);
    CHECK(q.integ <= (LIMI << 16), "plafond depasse");
    /* 1d. regime normal (sans butee) : resultat identique a l'integration libre */
    pi_t a = {0, 0}; int32_t ia = 0;
    for (int k = 0; k < 200; k++) { int32_t e = (k % 7) - 3; pas(&a, e, 0); ia += e * GI; if (ia > (LIMI << 16)) ia = LIMI << 16; }
    CHECK(a.integ == ia, "regime libre : integrale %d au lieu de %d", (int)a.integ, (int)ia);

    printf("[2] scenario 10 pts sous charge (E monte a 290 ticks, puis chute vers 30)\n");
    int u_fin, t30, t20; double imax;
    scenario(1, &u_fin, &t30, &imax, &t20);
    printf("    sortie : passage de 100 a 30 en %d periodes ; courant max apres la chute %.1f pts ; retour sous 20 pts "
           "apres %d periodes (%.1f ms)\n", t30, imax, t20, t20 * 0.0606);
    printf("== %d verifications OK, %d ECHECS -> %s\n", n_ok, n_ko, n_ko ? "ROUGE" : "VERT");
    return n_ko ? 1 : 0;
}
