/*
 * Test hote du correctif F4 garde (inc/f4_mode_commun.h, l'en-tete reel compile dans le firmware de banc).
 *   gcc -std=gnu99 -Wall -Wextra -Iinc tools/test_f4_mode_commun.c -o test_f4.exe
 *   gcc -std=gnu99 -Wall -Wextra -Iinc -DF4_LOGIQUE_ACTUELLE tools/test_f4_mode_commun.c -o test_f4_actuel.exe
 *   test_f4.exe [capture.csv]   (capture du velo velo-F3-essaiA-pre0.csv, colonnes periode,ph1,ph2,ph3 : rejeu)
 * Code de sortie 0 = tous les cas passent. La version F4_LOGIQUE_ACTUELLE (moyenne sans garde, 713f4dd) doit echouer.
 *
 * Modele de l'ISR du build de banc (main.c, ADC0_1_IRQHandler), CURRENT_SIGN = 1 : brut = courant + offset injecte ;
 * F4 ; etats dyn ADC 0/1 sans reconstruction (CCR <= ~1738) ; trip sur (ph1, ph2, -(ph1 + ph2)) : saturation immediate
 * a |i| >= 2000 (PH_SATURATION_COUNTS), seuil de banc 16 pts (PH_CURRENT_TRIP, variante 2).
 */
#include <stdio.h>
#include <stdlib.h>
#include "f4_mode_commun.h"

#define PH_SATURATION_COUNTS 2000
#define TRIP_BANC 16

static int n_ok = 0, n_ko = 0;
#define CHECK(cond, ...) do { if (cond) { n_ok++; } else { n_ko++; printf("  ECHEC : "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const char *nom(f4_resultat_t r)
{
    switch (r) {
    case F4_APPLIQUEE: return "appliquee";
    case F4_REFUS_MASQUE: return "refus masque";
    case F4_REFUS_SATURATION: return "refus saturation";
    case F4_REFUS_MODE_COMMUN: return "refus mode commun";
    }
    return "?";
}

static int a16(int v) { return v < 0 ? -v : v; }

typedef struct { int16_t i[3]; f4_resultat_t r; int crit; } sortie_t;

/* une periode de l'ISR : courants corriges d'offset, offsets injectes, masque -> F4 -> reconstruction d'EBiCS selon l'etat
   dyn ADC (0/1 : rien ; 2 : ph1 = -ph2-ph3 ; 3 : ph2 = -ph1-ph3) -> entrees du trip -> critere max |.| */
static sortie_t isr_etat(int i1, int i2, int i3, const int off[3], uint8_t masque, int etat)
{
    sortie_t s;
    s.i[0] = (int16_t)i1; s.i[1] = (int16_t)i2; s.i[2] = (int16_t)i3;
    int32_t b1 = i1 + off[0], b2 = i2 + off[1], b3 = i3 + off[2];
    s.r = f4_mode_commun(&s.i[0], &s.i[1], &s.i[2], b1, b2, b3, masque);
    const int16_t mes[3] = {s.i[0], s.i[1], s.i[2]};
    int16_t p1 = s.i[0], p2 = s.i[1];
    if (etat == 2) p1 = (int16_t)(-s.i[1] - s.i[2]);
    if (etat == 3) p2 = (int16_t)(-s.i[0] - s.i[2]);
    int16_t t[3];
    f4_entrees_trip(t, p1, p2, mes, masque);
    int c = 0;
    for (int k = 0; k < 3; k++) if (a16(t[k]) > c) c = a16(t[k]);
    s.crit = c;
    return s;
}
static sortie_t isr(int i1, int i2, int i3, const int off[3], uint8_t masque) { return isr_etat(i1, i2, i3, off, masque, 1); }

/* critere du trip le plus faible sur les quatre etats dyn ADC (la phase reconstruite depend de l'angle) */
static int crit_min_etats(int i1, int i2, int i3, const int off[3])
{
    int m = 1 << 30;
    for (int e = 0; e < 4; e++) {
        sortie_t s = isr_etat(i1, i2, i3, off, F4_MASQUE_TOUTES, e);
        if (s.crit < m) m = s.crit;
    }
    return m;
}

static const int OFF_2048[3] = {2048, 2048, 2048};
static const int OFF_VELO[3] = {2028, 2026, 2020};   /* zeros mesures sur le velo le 26/10 (t20) */

int main(int argc, char **argv)
{
#ifdef F4_LOGIQUE_ACTUELLE
    printf("== F4 : logique ACTUELLE (713f4dd, moyenne sans garde)\n");
#else
    printf("== F4 : logique GARDEE (masque, saturation brute, mode commun <= %d pts)\n", F4_MC_MAX);
#endif
    sortie_t s;

    printf("[1] voie unique enorme [2000,0,0] : refus, le trip voit 2000 (saturation immediate)\n");
    s = isr(2000, 0, 0, OFF_2048, F4_MASQUE_TOUTES);
    printf("    zero 2048 : %s -> [%d,%d,%d], critere trip %d\n", nom(s.r), s.i[0], s.i[1], s.i[2], s.crit);
    CHECK(s.r != F4_APPLIQUEE, "[2000,0,0] zero 2048 : F4 appliquee");
    CHECK(s.i[0] == 2000 && s.crit >= PH_SATURATION_COUNTS, "[2000,0,0] zero 2048 : trip voit %d < 2000", s.crit);
    s = isr(2000, 0, 0, OFF_VELO, F4_MASQUE_TOUTES);
    printf("    zeros velo : %s -> [%d,%d,%d], critere trip %d\n", nom(s.r), s.i[0], s.i[1], s.i[2], s.crit);
    CHECK(s.r != F4_APPLIQUEE, "[2000,0,0] zeros velo : F4 appliquee");
    CHECK(s.crit >= PH_SATURATION_COUNTS, "[2000,0,0] zeros velo : trip voit %d < 2000", s.crit);
    printf("    voie enorme sur chaque phase, critere du trip le plus faible sur les etats dyn ADC 0..3 :\n");
    for (int k = 0; k < 3; k++) {
        int v[3] = {0, 0, 0}; v[k] = 2000;
        int c = crit_min_etats(v[0], v[1], v[2], OFF_2048);
        int c2 = crit_min_etats(-v[0], -v[1], -v[2], OFF_2048);
        printf("      ph%d = +2000 : %d   ph%d = -2000 : %d\n", k + 1, c, k + 1, c2);
        CHECK(c >= PH_SATURATION_COUNTS && c2 >= PH_SATURATION_COUNTS, "ph%d seule +-2000 : trip voit %d / %d < 2000", k + 1, c, c2);
    }

    printf("[2] tout masque != 0b111 refuse (256 valeurs, bruit nominal [29,14,14])\n");
    int masques_ko = 0;
    for (int m = 0; m < 256; m++) {
        s = isr(29, 14, 14, OFF_2048, (uint8_t)m);
        if (m == F4_MASQUE_TOUTES) continue;
        if (s.r != F4_REFUS_MASQUE || s.i[0] != 29 || s.i[1] != 14 || s.i[2] != 14) masques_ko++;
    }
    printf("    masques != 0b111 non refuses : %d / 255\n", masques_ko);
    CHECK(masques_ko == 0, "%d masques != 0b111 acceptes", masques_ko);

    printf("[3] masque 0b111 nominal, bruit commun +-19 retire (vrai courant [10,-5,-5])\n");
    s = isr(10 + 19, -5 + 19, -5 + 19, OFF_VELO, F4_MASQUE_TOUTES);
    printf("    +19 : %s -> [%d,%d,%d]\n", nom(s.r), s.i[0], s.i[1], s.i[2]);
    CHECK(s.r == F4_APPLIQUEE && s.i[0] == 10 && s.i[1] == -5 && s.i[2] == -5, "+19 non retire");
    s = isr(10 - 19, -5 - 19, -5 - 19, OFF_VELO, F4_MASQUE_TOUTES);
    printf("    -19 : %s -> [%d,%d,%d]\n", nom(s.r), s.i[0], s.i[1], s.i[2]);
    CHECK(s.r == F4_APPLIQUEE && s.i[0] == 10 && s.i[1] == -5 && s.i[2] == -5, "-19 non retire");
    s = isr(12, -7, -5, OFF_VELO, F4_MASQUE_TOUTES);   /* courant de somme nulle : inchange */
    CHECK(s.r == F4_APPLIQUEE && s.i[0] == 12 && s.i[1] == -7 && s.i[2] == -5, "courant de somme nulle modifie");

    printf("[4] brut sature (butee a %d pts des rails)\n", F4_BRUT_MARGE);
    CHECK(f4_brut_sature(48) && f4_brut_sature(0) && !f4_brut_sature(49), "butee basse 48/49 mal placee");
    CHECK(f4_brut_sature(4047) && f4_brut_sature(4095) && !f4_brut_sature(4046), "butee haute 4046/4047 mal placee");
    s = isr(-2000, 900, 900, OFF_2048, F4_MASQUE_TOUTES);   /* ph1 brut = 48 */
    printf("    [-2000,900,900] brut ph1 48 : %s -> [%d,%d,%d], critere trip %d\n", nom(s.r), s.i[0], s.i[1], s.i[2], s.crit);
    CHECK(s.r == F4_REFUS_SATURATION, "brut 48 : %s au lieu de refus saturation", nom(s.r));
    CHECK(s.crit >= PH_SATURATION_COUNTS, "brut 48 : trip voit %d < 2000", s.crit);
    s = isr(5, 2047, -2040, OFF_2048, F4_MASQUE_TOUTES);    /* ph2 brut = 4095, somme 12 (petite) */
    printf("    [5,2047,-2040] brut ph2 4095 : %s\n", nom(s.r));
    CHECK(s.r == F4_REFUS_SATURATION, "brut 4095 : %s au lieu de refus saturation", nom(s.r));

    printf("[5] valeur limite du mode commun : |somme| <= %d appliquee, > %d refusee\n", 3 * F4_MC_MAX, 3 * F4_MC_MAX);
    s = isr(24, 24, 24, OFF_2048, F4_MASQUE_TOUTES);
    printf("    somme  72 : %s -> [%d,%d,%d]\n", nom(s.r), s.i[0], s.i[1], s.i[2]);
    CHECK(s.r == F4_APPLIQUEE && s.i[0] == 0 && s.i[1] == 0 && s.i[2] == 0, "somme 72 non appliquee");
    s = isr(25, 24, 24, OFF_2048, F4_MASQUE_TOUTES);
    printf("    somme  73 : %s -> [%d,%d,%d]\n", nom(s.r), s.i[0], s.i[1], s.i[2]);
    CHECK(s.r == F4_REFUS_MODE_COMMUN && s.i[0] == 25, "somme 73 non refusee");
    s = isr(-24, -24, -24, OFF_2048, F4_MASQUE_TOUTES);
    CHECK(s.r == F4_APPLIQUEE && s.i[0] == 0, "somme -72 non appliquee");
    s = isr(-25, -24, -24, OFF_2048, F4_MASQUE_TOUTES);
    printf("    somme -73 : %s -> [%d,%d,%d]\n", nom(s.r), s.i[0], s.i[1], s.i[2]);
    CHECK(s.r == F4_REFUS_MODE_COMMUN && s.i[0] == -25, "somme -73 non refusee");

    printf("[6] voie fausse moyenne (non saturee) sur chaque phase : refus, le trip voit 200 dans tous les etats\n");
    s = isr(200, 0, 0, OFF_VELO, F4_MASQUE_TOUTES);
    printf("    [200,0,0] : %s -> [%d,%d,%d]\n", nom(s.r), s.i[0], s.i[1], s.i[2]);
    CHECK(s.r == F4_REFUS_MODE_COMMUN, "[200,0,0] : %s", nom(s.r));
    for (int k = 0; k < 3; k++) {
        int v[3] = {0, 0, 0}; v[k] = 200;
        int c = crit_min_etats(v[0], v[1], v[2], OFF_VELO);
        printf("    ph%d = 200 : critere trip min %d\n", k + 1, c);
        CHECK(c >= 200, "ph%d = 200 : trip voit %d < 200", k + 1, c);
    }

    printf("[7] courant reel de somme nulle, F4 appliquee : le trip voit la meme chose qu'avant (etats 0..3)\n");
    {
        int c = crit_min_etats(300, -100, -200, OFF_VELO);
        printf("    [300,-100,-200] : critere trip min %d\n", c);
        CHECK(c == 300, "courant reel : critere %d au lieu de 300", c);
    }

    if (argc > 1) {
        FILE *f = fopen(argv[1], "r");
        if (!f) { printf("capture %s illisible\n", argv[1]); return 2; }
        char ligne[128];
        int n = 0, refus = 0, crit_max = 0, crit_brut_max = 0, smax = 0, au_trip = 0;
        if (!fgets(ligne, sizeof ligne, f)) { fclose(f); return 2; }
        while (fgets(ligne, sizeof ligne, f)) {
            int p, a, b, c;
            if (sscanf(ligne, "%d,%d,%d,%d", &p, &a, &b, &c) != 4) continue;
            n++;
            if (a16(a + b + c) > smax) smax = a16(a + b + c);
            int cb = a16(a); if (a16(b) > cb) cb = a16(b); if (a16(a + b) > cb) cb = a16(a + b);
            if (cb > crit_brut_max) crit_brut_max = cb;
            s = isr(a, b, c, OFF_VELO, F4_MASQUE_TOUTES);
            if (s.r != F4_APPLIQUEE) refus++;
            if (s.crit > crit_max) crit_max = s.crit;
            if (s.crit >= TRIP_BANC) au_trip++;
        }
        fclose(f);
        printf("[8] rejeu capture velo (%d periodes) : |somme| max %d, critere trip sans F4 %d, avec F4 %d, "
               "periodes >= %d : %d, refus F4 : %d\n", n, smax, crit_brut_max, crit_max, TRIP_BANC, au_trip, refus);
        CHECK(n == 128 && refus == 0 && au_trip == 0, "rejeu : %d refus, %d periodes au trip", refus, au_trip);
    }

    printf("== %d verifications OK, %d ECHECS -> %s\n", n_ok, n_ko, n_ko ? "ROUGE" : "VERT");
    return n_ko ? 1 : 0;
}
