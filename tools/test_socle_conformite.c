/*
 * Test hote du socle de conformite (inc/socle_conformite.h, l'en-tete reel compile dans le firmware).
 *   gcc -std=gnu99 -Wall -Wextra -Iinc tools/test_socle_conformite.c -o test_socle_conformite.exe
 * Valeurs attendues : validation/porte-4/socle-conformite-attendu-20261028.md.
 */
#include <stdio.h>
#include "socle_conformite.h"

static int n_ok = 0, n_ko = 0;
#define CHECK(cond, ...) do { if (cond) { n_ok++; } else { n_ko++; printf("  ECHEC : "); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define IBAT_MAX 15000          /* BATTERYCURRENT_MAX du build route (mA) */
#define PTS_PAR_A_X100 314      /* PH_COUNTS_PER_A_X100, variante 2 */

/* simule n secondes a 4 kHz par pas de 100 ticks ; renvoie le niveau applique a la fin, compte les ticks en L4 */
static uint8_t simule(socle_l5_t *t, uint32_t secondes, uint8_t demande, uint32_t *ticks_l4)
{
    uint8_t n = demande;
    for (uint32_t i = 0; i < secondes * 40u; i++) {
        n = socle_l5_pas(t, 100u, demande);
        if (ticks_l4 && n == 4u) *ticks_l4 += 100u;
    }
    return n;
}

int main(void)
{
    /* 1. R2 batterie par niveau */
    static const int32_t ibat[6] = {5400, 5400, 9000, 10800, 12600, 15000};
    for (uint8_t n = 0; n < 6; n++)
        CHECK(socle_ibat_niveau_mA(n, IBAT_MAX) == ibat[n], "Ibat niveau %u : %ld", n, (long)socle_ibat_niveau_mA(n, IBAT_MAX));
    CHECK(socle_ibat_niveau_mA(9, IBAT_MAX) == 15000, "niveau hors table ecrete a 5");

    /* 2. R3 phase 1,75 x : 0, 29, 49, 59, 69, 82 pts sans ecretage ; ecretage a 30 ; a 120 rien n'est ecrete */
    static const int32_t ph[6] = {0, 29, 49, 59, 69, 82};
    for (uint8_t n = 0; n < 6; n++) {
        int32_t p = socle_phase_niveau_pts(n, socle_ibat_niveau_mA(n, IBAT_MAX), PTS_PAR_A_X100, 1000);
        CHECK(p == ph[n], "phase niveau %u : %ld (attendu %ld)", n, (long)p, (long)ph[n]);
        CHECK(socle_phase_niveau_pts(n, socle_ibat_niveau_mA(n, IBAT_MAX), PTS_PAR_A_X100, 30) == (ph[n] > 30 ? 30 : ph[n]),
              "phase niveau %u ecretee a 30", n);
        CHECK(socle_phase_niveau_pts(n, socle_ibat_niveau_mA(n, IBAT_MAX), PTS_PAR_A_X100, 120) == ph[n], "niveau %u sous 120", n);
        /* lot B2 (29/10) : plafond 60, niveaux 1 a 3 = 29 / 49 / 59 non ecretes, 4 et 5 ecretes a 60 */
        CHECK(socle_phase_niveau_pts(n, socle_ibat_niveau_mA(n, IBAT_MAX), PTS_PAR_A_X100, 60) == (ph[n] > 60 ? 60 : ph[n]),
              "phase niveau %u ecretee a 60 (lot B2)", n);
    }

    /* lot B3 (30/10) : base 18 A, plafond 98 : 6,48 / 10,8 / 12,96 / 15,12 / 18 A et 35 / 59 / 71 / 83 / 98 pts */
    {
        static const int32_t ib18[6] = {6480, 6480, 10800, 12960, 15120, 18000}, ph18[6] = {0, 35, 59, 71, 83, 98};
        for (uint8_t n = 0; n < 6; n++) {
            CHECK(socle_ibat_niveau_mA(n, 18000) == ib18[n], "18 A : Ibat niveau %u : %ld", n, (long)socle_ibat_niveau_mA(n, 18000));
            int32_t p18 = socle_phase_niveau_pts(n, socle_ibat_niveau_mA(n, 18000), PTS_PAR_A_X100, 98);
            CHECK(p18 == ph18[n], "18 A : phase niveau %u : %ld (attendu %ld)", n, (long)p18, (long)ph18[n]);
        }
    }

    /* 3. codes de niveau <-> indices (level_to_array_element de main.c) */
    {
        static const uint8_t level_to_array_element[10] = {0, 0, 1, 0, 2, 0, 3, 0, 4, 5};
        int ok = 1;
        for (uint8_t n = 0; n < 6; n++) if (level_to_array_element[socle_code_niveau[n]] != n) ok = 0;
        CHECK(ok, "aller-retour code / indice");
        CHECK(socle_code_niveau[1] == 2 && socle_code_niveau[4] == 8 && socle_code_niveau[5] == 9, "codes 2, 8, 9");
    }

    /* 4. R5 timer L5 */
    {
        socle_l5_t t = SOCLE_L5_INIT;
        uint32_t l4 = 0;
        CHECK(simule(&t, 119, 5, &l4) == 5 && l4 == 0, "L5 continu : L5 a 119 s");
        CHECK(simule(&t, 1, 5, &l4) == 4 && t.bloque, "L5 continu : L4 impose a 120 s");
        simule(&t, 479, 5, &l4);
        CHECK(socle_l5_pas(&t, 0, 5) == 4, "L5 continu : encore L4 a 599 s");
        CHECK(simule(&t, 1, 5, &l4) == 5 && !t.bloque, "L5 continu : L5 de nouveau a 600 s");
        CHECK(l4 >= 480u * 4000u - 100u && l4 <= 480u * 4000u + 100u, "duree en L4 = 8 min (%lu ticks)", (unsigned long)l4);
        CHECK(t.cumul < 4000u, "cumul remis a zero en fin de repos");
    }
    {
        socle_l5_t t = SOCLE_L5_INIT;
        CHECK(simule(&t, 60, 5, NULL) == 5, "L5 60 s");
        CHECK(simule(&t, 600, 3, NULL) == 3 && t.cumul == 60u * 4000u, "L3 10 min : cumul garde (%lu)", (unsigned long)t.cumul);
        CHECK(simule(&t, 59, 5, NULL) == 5, "retour L5 : encore L5 apres 59 s de plus");
        CHECK(simule(&t, 1, 5, NULL) == 4, "retour L5 : L4 apres 60 s de plus (cumul 120 s)");
    }
    {
        socle_l5_t t = SOCLE_L5_INIT;
        simule(&t, 120, 5, NULL);
        int ok = 1;
        for (uint8_t d = 0; d <= 4; d++) if (socle_l5_pas(&t, 100u, d) != d) ok = 0;
        CHECK(ok && t.bloque, "pendant le repos, les demandes 0 a 4 sont appliquees telles quelles");
        CHECK(socle_l5_pas(&t, 0, 5) == 4, "pendant le repos, L5 donne L4");
    }

    /* 5. R1 rampe de bride a 15 km/h : pleine sous 13, moitie a 15, 0 a 17 */
    CHECK(socle_bride(30, 1299, 1500) == 30, "12,99 km/h : pleine");
    CHECK(socle_bride(30, 1300, 1500) == 30, "13 km/h : pleine");
    CHECK(socle_bride(30, 1500, 1500) == 15, "15 km/h : moitie");
    CHECK(socle_bride(30, 1700, 1500) == 0, "17 km/h : 0");
    CHECK(socle_bride(30, 1701, 1500) == 0, "17,01 km/h : 0");
    CHECK(socle_bride(0, 1400, 1500) == 0 && socle_bride(0, 1000, 1500) == 0, "consigne nulle reste nulle");
    {
        int mono = 1; int32_t prec = 30;
        for (int32_t v = 1200; v <= 1800; v++) { int32_t c = socle_bride(30, v, 1500); if (c > prec) mono = 0; prec = c; }
        CHECK(mono, "rampe decroissante");
    }
    CHECK(socle_bride(30, 2300, 2500) == 30 && socle_bride(30, 2500, 2500) == 15 && socle_bride(30, 2700, 2500) == 0,
          "a 25 km/h : 23 -> 27 comme le Z8");

    printf("niveaux 0-5 : Ibat %ld/%ld/%ld/%ld/%ld/%ld mA, phase %ld/%ld/%ld/%ld/%ld/%ld pts\n",
           (long)ibat[0], (long)ibat[1], (long)ibat[2], (long)ibat[3], (long)ibat[4], (long)ibat[5],
           (long)ph[0], (long)ph[1], (long)ph[2], (long)ph[3], (long)ph[4], (long)ph[5]);
    printf("== %d verifications OK, %d ECHECS -> %s\n", n_ok, n_ko, n_ko ? "ROUGE" : "VERT");
    return n_ko ? 1 : 0;
}
