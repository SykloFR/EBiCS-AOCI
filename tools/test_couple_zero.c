/*
 * Test hote du zero de couple adaptatif (inc/couple_zero.h, l'en-tete reel compile dans le firmware).
 *   gcc -std=gnu99 -Wall -Wextra -Iinc tools/test_couple_zero.c -o test_couple_zero.exe -lm
 *   (et avec -DROUTE_BUILD : fenetre CZ_N = 48 au lieu de 20, 29/10)
 * Regle (origine) : zero = (2e + 3e plus petites des CZ_N dernieres) / 2, 750 pendant l'amorce, hors 300-4900 ignore.
 * Cas chiffres avec le filtre EBiCS par impulsion (TQFILTER 4) : effort constant, coup de pedale, rotation sans effort.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "couple_zero.h"

static int n_ok = 0, n_ko = 0;
#define CHECK(cond, ...) do { if (cond) { n_ok++; } else { n_ko++; printf("  ECHEC : "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int cmp(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

/* filtre EBiCS par impulsion sur une suite de couples, renvoie le couple filtre final et le zero final */
static int filtre(couple_zero_t *c, const int *mv, int n, int zero_fixe, int *zero_fin)
{
    int32_t cum = 0;
    int z = zero_fixe;
    for (int i = 0; i < n; i++) {
        cum -= cum >> 4;
        if (!zero_fixe) z = couple_zero_ajoute(c, mv[i]);
        if (mv[i] > z) cum += mv[i] - z;
    }
    if (zero_fin) *zero_fin = z;
    return (int)(cum >> 4);
}

int main(void)
{
    printf("CZ_N = %u\n", CZ_N);
    /* 1. amorce : 750 tant que n < CZ_N, quelle que soit la valeur */
    {
        couple_zero_t c = COUPLE_ZERO_INIT;
        int ok = 1;
        for (int i = 0; i < (int)CZ_N - 1; i++) if (couple_zero_ajoute(&c, 1000 + i) != 750) ok = 0;
        CHECK(ok && c.n == CZ_N - 1, "amorce : n %u", c.n);
        CHECK(couple_zero_ajoute(&c, 1000 + (int)CZ_N - 1) == (1001 + 1002) / 2, "CZ_N-ieme valeur : zero %u", c.zero);
    }

    /* 2. regle exacte contre un tri de reference, suites aleatoires, avec valeurs hors fenetre */
    srand(5);
    for (int t = 0; t < 500; t++) {
        couple_zero_t c = COUPLE_ZERO_INIT;
        int hist[CZ_N], nh = 0, ok = 1;
        for (int i = 0; i < 200; i++) {
            int v = (rand() % 10 == 0) ? (rand() % 2 ? 100 + rand() % 199 : 4901 + rand() % 500) : 300 + rand() % 4601;
            uint16_t z = couple_zero_ajoute(&c, v);
            if (v >= CZ_MIN_MV && v <= CZ_MAX_MV) {
                if (nh < (int)CZ_N) hist[nh++] = v; else { for (int k = 1; k < (int)CZ_N; k++) hist[k - 1] = hist[k]; hist[CZ_N - 1] = v; }
            }
            int attendu = 750;
            if (nh == (int)CZ_N) { int s[CZ_N]; for (int k = 0; k < (int)CZ_N; k++) s[k] = hist[k]; qsort(s, CZ_N, sizeof(int), cmp); attendu = (s[1] + s[2]) / 2; }
            if (z != attendu) { ok = 0; printf("  suite %d pas %d : %u au lieu de %d\n", t, i, z, attendu); break; }
        }
        CHECK(ok, "suite aleatoire %d", t);
    }

    /* 3. cas chiffres (200 impulsions chacun) */
    int mv[200];
    int zf;
    /* 3a. rotation sans effort : 754 a 789 mV (capture 2f45b33) */
    srand(7);
    for (int i = 0; i < 200; i++) mv[i] = 754 + rand() % 36;
    { couple_zero_t c = COUPLE_ZERO_INIT; int f750 = filtre(&c, mv, 200, 750, NULL); couple_zero_t c2 = COUPLE_ZERO_INIT; int fad = filtre(&c2, mv, 200, 0, &zf);
      printf("rotation sans effort 754-789 : filtre %d (zero 750) -> %d (zero adaptatif %d)\n", f750, fad, zf);
      /* bruit uniforme, plus defavorable que la capture (rejeu : 19 -> 9) : seulement la baisse est exigee */
      CHECK(fad < f750 && zf >= 754 && zf <= 770, "sans effort : %d -> %d, zero %d", f750, fad, zf); }
    /* 3b. effort constant 1200 mV : le zero rejoint l'effort, l'assistance tombe (propriete de l'origine) */
    for (int i = 0; i < 200; i++) mv[i] = 1200;
    { couple_zero_t c = COUPLE_ZERO_INIT; int f750 = filtre(&c, mv, 200, 750, NULL); couple_zero_t c2 = COUPLE_ZERO_INIT; int fad = filtre(&c2, mv, 200, 0, &zf);
      printf("effort constant 1200 : filtre %d (zero 750) -> %d (zero adaptatif %d)\n", f750, fad, zf);
      CHECK(f750 == 450 &&fad == 0 && zf == 1200, "constant : %d -> %d, zero %d", f750, fad, zf); }
    /* 3c. coup de pedale : 750 a 1500 mV, deux bosses par tour (periode 24 impulsions) */
    for (int i = 0; i < 200; i++) mv[i] = 750 + (int)(750.0 * fabs(sin(3.14159265 * i / 24.0)));
    { couple_zero_t c = COUPLE_ZERO_INIT; int f750 = filtre(&c, mv, 200, 750, NULL); couple_zero_t c2 = COUPLE_ZERO_INIT; int fad = filtre(&c2, mv, 200, 0, &zf);
      printf("coup de pedale 750-1500 : filtre %d (zero 750) -> %d (zero adaptatif %d)\n", f750, fad, zf);
      CHECK(zf <= 900 && fad >= f750 / 2, "coup de pedale : %d -> %d, zero %d", f750, fad, zf); }

    printf("== %d verifications OK, %d ECHECS -> %s\n", n_ok, n_ko, n_ko ? "ROUGE" : "VERT");
    return n_ko ? 1 : 0;
}
