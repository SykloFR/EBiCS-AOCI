/*
 * Test hote de la temperature controleur par la table NTC d'origine (inc/ntc_aoci.h, l'en-tete reel du firmware).
 *   gcc -std=gnu99 -Wall -Wextra -Iinc tools/test_ntc_aoci.c -o test_ntc_aoci.exe
 * Valeurs attendues : validation/porte-4/correctif-ntc-controleur-attendu-20261029.md.
 */
#include <stdio.h>
#include "ntc_aoci.h"

static int n_ok = 0, n_ko = 0;
#define CHECK(cond, ...) do { if (cond) { n_ok++; } else { n_ko++; printf("  ECHEC : "); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void)
{
    /* 1. table : bornes d'origine, puis chaque point donne exactement sa temperature (hors point 36 hors ordre) */
    CHECK(ntc_aoci_table[0] == 1007u && ntc_aoci_table[190] == 36u, "bornes 1007 / 36");
    CHECK(ntc_aoci_table[35] == 912u && ntc_aoci_table[36] == 874u && ntc_aoci_table[37] == 902u, "points 35-37 d'origine");
    {
        int ok = 1, k_ko = -1;
        for (int k = 0; k < 191; k++) {
            if (k == 36 || k == 37) continue;                     /* points hors ordre d'origine (874, 902) */
            if (k > 0 && ntc_aoci_table[k] == ntc_aoci_table[k - 1]) continue;   /* valeur repetee (39, 39) : le premier point fait foi */
            if (ntc_aoci_dixiemes((uint16_t)(4u * ntc_aoci_table[k])) != (k - 40) * 10) { ok = 0; k_ko = k; break; }
        }
        CHECK(ok, "chaque point = i - 40 degres (premier ecart au point %d)", k_ko);
    }

    /* 2. points mesures : B1.1 (29/10) et 28/10 */
    CHECK(ntc_aoci_dixiemes(3047) == 161, "ADC 3047 -> 16,1 (%d)", ntc_aoci_dixiemes(3047));
    CHECK(ntc_aoci_dixiemes(3000) == 174, "ADC 3000 -> 17,4 (%d)", ntc_aoci_dixiemes(3000));
    CHECK(ntc_aoci_c(3047) == 16 && ntc_aoci_c(3091) == 15, "arrondi en degres : 16 et 15");

    /* 3. seuils de la reduction de puissance */
    CHECK(ntc_aoci_dixiemes(712) == 850, "85 degres <-> ADC 712 (%d)", ntc_aoci_dixiemes(712));
    CHECK(ntc_aoci_dixiemes(480) == 1000, "100 degres <-> ADC 480 (%d)", ntc_aoci_dixiemes(480));
    CHECK(ntc_aoci_c(712) == 85 && ntc_aoci_c(480) == 100, "85 et 100 en degres");

    /* 4. saturation */
    CHECK(ntc_aoci_dixiemes(4028) == -400 && ntc_aoci_dixiemes(4095) == -400, "ADC >= 4028 -> -40");
    CHECK(ntc_aoci_dixiemes(144) == 1500 && ntc_aoci_dixiemes(0) == 1500 && ntc_aoci_dixiemes(143) == 1500, "ADC <= 144 -> 150");
    CHECK(ntc_aoci_c(4095) == -40 && ntc_aoci_c(0) == 150, "saturation en degres");

    /* 5. la temperature ne monte jamais quand l'ADC monte */
    {
        int mono = 1, prec = 2000, a_ko = -1;
        for (int a = 0; a <= 4095; a++) {             /* toute la plage, zone hors ordre comprise (recherche cote chaud) */
            int t = ntc_aoci_dixiemes((uint16_t)a);
            if (t > prec) { mono = 0; a_ko = a; break; }
            prec = t;
        }
        CHECK(mono, "decroissante (ecart a ADC %d)", a_ko);
    }

    /* 6. zone hors ordre : erreur bornee a 2 degres (vrai : interpolation 912 -> 902 sur -5 -> -3) */
    {
        int pire = 0;
        for (int a10 = 902; a10 < 912; a10++) {
            int vrai = -50 + 20 * (912 - a10) / 10, t = ntc_aoci_dixiemes((uint16_t)(4 * a10)), e = t > vrai ? t - vrai : vrai - t;
            if (e > pire) pire = e;
        }
        CHECK(pire <= 20, "zone 902-911 : erreur max %d dixiemes (borne 2 degres)", pire);
        CHECK(ntc_aoci_dixiemes(4 * 896) == -20 && ntc_aoci_dixiemes(4 * 878) == 10, "896 -> -2,0 et 878 -> +1,0 (hors zone)");
    }

    {
        int pire = 0;
        for (int a10 = 902; a10 < 912; a10++) {
            int v = -50 + 20 * (912 - a10) / 10, t = ntc_aoci_dixiemes((uint16_t)(4 * a10)), e = t > v ? t - v : v - t;
            if (e > pire) pire = e;
        }
        printf("zone hors ordre 902-911 : erreur max %d dixiemes\n", pire);
    }
    printf("ADC 3047 -> %d dixiemes, 3000 -> %d, 712 -> %d, 480 -> %d\n", ntc_aoci_dixiemes(3047), ntc_aoci_dixiemes(3000),
           ntc_aoci_dixiemes(712), ntc_aoci_dixiemes(480));
    printf("== %d verifications OK, %d ECHECS -> %s\n", n_ok, n_ko, n_ko ? "ROUGE" : "VERT");
    return n_ko ? 1 : 0;
}
