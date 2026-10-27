/*
 * Test hote du maintien de MOE en rotation (inc/moe_rotation.h, l'en-tete reel compile dans le firmware).
 *   gcc -std=gnu99 -Wall -Wextra -Iinc tools/test_moe_rotation.c -o test_moe_rotation.exe
 * Table de verite, puis scenario : moteur lance, consigne qui passe par 0, deceleration jusqu'a l'arret.
 */
#include <stdio.h>
#include "moe_rotation.h"

static int n_ok = 0, n_ko = 0;
#define CHECK(cond, ...) do { if (cond) { n_ok++; } else { n_ko++; printf("  ECHEC : "); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void)
{
    /* 1. table de verite */
    static const int32_t cons[] = {0, 1, 5, -1};
    static const uint16_t ticks[] = {0, 1, 399, 400, 401, 4000, 64000};
    for (unsigned c = 0; c < 4; c++)
        for (int p = 0; p < 2; p++)
            for (unsigned t = 0; t < 7; t++) {
                uint8_t attendu = (cons[c] == 0 && p && ticks[t] > 400) ? 1 : 0;
                CHECK(route_coupe_moe(cons[c], (uint8_t)p, ticks[t]) == attendu, "coupe(%ld, %d, %u)", (long)cons[c], p, ticks[t]);
            }
    CHECK(route_raz_pi_autorisee(0) == 1 && route_raz_pi_autorisee(1) == 0, "raz PI");

    /* 2. scenario a 4 kHz : erps 120 constant 2 s, consigne alternant 1 / 0 ; puis deceleration lineaire de 120 a 0 en 1 s,
          consigne 0 ; puis arret. Fronts Hall tous les 4000 / (6 x erps) ticks. Une seule coupure attendue, apres l'arret. */
    {
        uint8_t pwm = 1;
        uint16_t sans_hall = 0;
        int coupures = 0, t_coupure = -1, t_dernier_front = -1;
        double phase = 0;
        for (int t = 0; t < 4 * 4000; t++) {
            double erps = t < 8000 ? 120.0 : (t < 12000 ? 120.0 * (12000 - t) / 4000.0 : 0.0);
            int32_t consigne = t < 8000 ? ((t / 500) % 2) : 0;   /* passe par 0 toutes les 125 ms */
            phase += 6.0 * erps / 4000.0;                         /* fronts Hall par tick */
            if (sans_hall < ROUTE_TICKS_SAT) sans_hall++;          /* TIMER1 */
            if (phase >= 1.0) { phase -= 1.0; sans_hall = 0; t_dernier_front = t; }   /* TIMER2 */
            if (route_coupe_moe(consigne, pwm, sans_hall)) { pwm = 0; coupures++; t_coupure = t; }
        }
        printf("scenario : dernier front Hall a %d ticks, coupure a %d ticks (%d coupure(s))\n", t_dernier_front, t_coupure, coupures);
        CHECK(coupures == 1, "une seule coupure (%d)", coupures);
        CHECK(t_coupure > t_dernier_front && t_coupure - t_dernier_front == 401, "coupure 401 ticks apres le dernier front");
        CHECK(t_coupure >= 8000, "aucune coupure pendant la rotation a erps 120 malgre la consigne a 0");
    }
    printf("== %d verifications OK, %d ECHECS -> %s\n", n_ok, n_ko, n_ko ? "ROUGE" : "VERT");
    return n_ko ? 1 : 0;
}
