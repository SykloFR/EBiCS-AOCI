/*
 * Test hote du diagnostic du pedalier (inc/pedalier_diag.h, l'en-tete reel compile dans le firmware).
 *   gcc -std=gnu99 -Wall -Wextra -Iinc tools/test_pedalier_diag.c -o test_pedalier_diag.exe
 * Sequences de niveaux connues : fronts comptes = transitions reelles, broches independantes, retour a 0 sur 16 bits.
 */
#include <stdio.h>
#include <stdlib.h>
#include "pedalier_diag.h"

static int n_ok = 0, n_ko = 0;
#define CHECK(cond, ...) do { if (cond) { n_ok++; } else { n_ko++; printf("  ECHEC : "); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void)
{
    /* 1. premier echantillon : aucun front, quel que soit le niveau */
    for (int a = 0; a < 2; a++)
        for (int b = 0; b < 2; b++) {
            pedalier_diag_t s = {0};
            pedalier_echantillon(&s, (uint8_t)a, (uint8_t)b);
            CHECK(s.fronts_pc12 == 0 && s.fronts_pd2 == 0 && s.niveaux == (a | b << 1),
                  "amorce %d %d : fronts %u %u niveaux %u", a, b, s.fronts_pc12, s.fronts_pd2, s.niveaux);
        }

    /* 2. PD2 seule : 48 impulsions (96 fronts), echantillons repetes entre les fronts, PC12 tenue haute */
    {
        pedalier_diag_t s = {0};
        int pd2 = 1;
        pedalier_echantillon(&s, 1, (uint8_t)pd2);
        for (int f = 0; f < 96; f++) {
            pd2 ^= 1;
            for (int k = 0; k < 20; k++) pedalier_echantillon(&s, 1, (uint8_t)pd2);
        }
        CHECK(s.fronts_pd2 == 96 && s.fronts_pc12 == 0 && s.niveaux == 3,   /* PC12 haute, PD2 revenue haute */
              "PD2 seule : %u %u", s.fronts_pd2, s.fronts_pc12);
    }

    /* 3. quadrature : PC12 et PD2 decales d'un quart de periode, 24 periodes -> 48 fronts chacune */
    {
        pedalier_diag_t s = {0};
        static const uint8_t q[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        pedalier_echantillon(&s, 0, 0);
        for (int p = 0; p < 24 * 4; p++) {
            const uint8_t *e = q[(p + 1) % 4];
            for (int k = 0; k < 5; k++) pedalier_echantillon(&s, e[0], e[1]);
        }
        CHECK(s.fronts_pc12 == 48 && s.fronts_pd2 == 48, "quadrature : %u %u", s.fronts_pc12, s.fronts_pd2);
    }

    /* 4. sequences aleatoires contre un compteur de reference */
    srand(1);
    for (int t = 0; t < 200; t++) {
        pedalier_diag_t s = {0};
        int p0 = -1, d0 = -1;
        unsigned rp = 0, rd = 0;
        for (int i = 0; i < 1000; i++) {
            int a = rand() & 1, b = (rand() % 3) ? (d0 < 0 ? 0 : d0) : (rand() & 1);
            if (p0 >= 0 && a != p0) rp++;
            if (d0 >= 0 && b != d0) rd++;
            p0 = a; d0 = b;
            pedalier_echantillon(&s, (uint8_t)(a ? 7 : 0), (uint8_t)(b ? 0x80 : 0));   /* valeurs non 0/1 acceptees */
        }
        CHECK(s.fronts_pc12 == rp && s.fronts_pd2 == rd && s.niveaux == (p0 | d0 << 1),
              "aleatoire %d : %u/%u %u/%u", t, s.fronts_pc12, rp, s.fronts_pd2, rd);
    }

    /* 5. retour a 0 apres 65535 */
    {
        pedalier_diag_t s = {0};
        int v = 0;
        pedalier_echantillon(&s, 0, 0);
        for (long f = 0; f < 65537; f++) { v ^= 1; pedalier_echantillon(&s, (uint8_t)v, 0); }
        CHECK(s.fronts_pc12 == 1 && s.fronts_pd2 == 0, "16 bits : %u", s.fronts_pc12);
    }

    printf("== %d verifications OK, %d ECHECS -> %s\n", n_ok, n_ko, n_ko ? "ROUGE" : "VERT");
    return n_ko ? 1 : 0;
}
