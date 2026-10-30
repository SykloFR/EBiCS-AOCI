/*
 * Test hote de la loi type OSF hybride du build route (29/10/2026) : plancher de cadence (inc/loi_osf.h, en-tete reel)
 * et profil par niveau (ROUTE_PROFIL_NIVEAU de inc/config.h). La loi EBiCS est recopiee d'update_setpoint :
 * consigne = TS_coeff x cadence^(1/11) x couple filtre x 0,0005 x profil, puis plancher, puis ecretage au plafond.
 *   gcc -std=gnu99 -Wall -Wextra -DROUTE_BUILD -DBENCH_BUILD -DCURRENT_SIGN=1 -Iinc tools/test_loi_osf.c -o test_loi_osf.exe -lm
 * Attendu : validation/porte-4/loi-type-osf-attendu-20261029.md.
 */
#include <math.h>
#include <stdio.h>
#include "config.h"
#include "loi_osf.h"

static int n_ok = 0, n_ko = 0;
#define CHECK(cond, ...) do { if (cond) { n_ok++; } else { n_ko++; printf("  ECHEC : "); printf(__VA_ARGS__); printf("\n"); } } while (0)

#ifndef ROUTE_BUILD
#error "test du build route : compiler avec -DROUTE_BUILD -DBENCH_BUILD -DCURRENT_SIGN=1"
#endif

static const int profil_route[5] = ROUTE_PROFIL_NIVEAU;

/* loi d'update_setpoint (TS_COEF 4, exposant 1/(1+10)), plancher en route, ecretage au plafond du niveau */
static int32_t consigne(int cadence, int couple, int profil, int plancher, int32_t plafond)
{
    int32_t c = (int32_t)((float)(TS_COEF * powf((float)cadence, 1.0f / 11.0f)) * couple * 0.0005 * profil);
    if (plancher) c = loi_osf_plancher(c, cadence);
    return c > plafond ? plafond : c;
}

int main(void)
{
    /* 1. plancher : identite a 40 tr/min et au-dela, x 2 a 20, x 4 a 10 et en dessous, sans debordement */
    CHECK(loi_osf_plancher(30, 40) == 30 && loi_osf_plancher(30, 90) == 30, "identite a 40 et 90");
    CHECK(loi_osf_plancher(30, 20) == 60, "x 2 a 20 : %ld", (long)loi_osf_plancher(30, 20));
    CHECK(loi_osf_plancher(30, 10) == 120 && loi_osf_plancher(30, 5) == 120 && loi_osf_plancher(30, 0) == 120, "x 4 a 10, 5 et 0");
    CHECK(loi_osf_plancher(0, 20) == 0 && loi_osf_plancher(-5, 20) == -5, "consigne nulle ou negative inchangee");
    CHECK(loi_osf_plancher(1000, 10) == 4000, "1000 a 10 tr/min : %ld", (long)loi_osf_plancher(1000, 10));
    int mono = 1;
    for (int c = 1; c < 60; c++) if (loi_osf_plancher(100, c) < loi_osf_plancher(100, c + 1)) mono = 0;
    CHECK(mono, "plancher decroissant avec la cadence");

    /* 2. profil route */
    static const int attendu[5] = {31, 43, 57, 67, 140};   /* 30/10 soir : niveau 5 110 -> 140 */
      /* lot B3 (30/10) : niveau 3 63 -> 57 */
    int ok = 1;
    for (int k = 0; k < 5; k++) if (profil_route[k] != attendu[k]) ok = 0;
    CHECK(ok, "ROUTE_PROFIL_NIVEAU = 31 / 43 / 57 / 67 / 140");

    /* 3. rapports a 50 tr/min (pas de plancher), couple filtre 190 mV (B1.3) et 300 mV, contre l'avant (20/40/60) */
    for (int t = 190; t <= 300; t += 110) {
        double r1 = (double)consigne(50, t, 31, 1, 1000) / consigne(50, t, 20, 0, 1000);
        double r2 = (double)consigne(50, t, 43, 1, 1000) / consigne(50, t, 40, 0, 1000);
        double r3 = (double)consigne(50, t, profil_route[2], 1, 1000) / consigne(50, t, 60, 0, 1000);
        printf("couple %d mV, 50 tr/min : niveau 1 x %.3f, niveau 2 x %.3f, niveau 3 x %.3f\n", t, r1, r2, r3);
        CHECK(fabs(r2 - 1.075) <= 0.03, "niveau 2 x %.3f (attendu 1,08)", r2);
        CHECK(fabs(r3 - 0.95) <= 0.03, "niveau 3 x %.3f (attendu 0,95 depuis le lot B3)", r3);
        CHECK(fabs(r1 - 1.55) <= 0.06, "niveau 1 x %.3f (attendu 1,55 ; consigne entiere de 6 a 18 pts : +-0,06)", r1);
    }

    /* 4. basse cadence (pedalier qui cale, B2 phase F) : niveau 3, plafond 59 ; cas choisis sous le plafond avant */
    int32_t a20 = consigne(20, 250, 60, 0, 59), b20 = consigne(20, 250, 63, 1, 59);
    int32_t a10 = consigne(10, 60, 60, 0, 59), b10 = consigne(10, 60, 63, 1, 59);
    printf("20 tr/min, 250 mV : %ld -> %ld ; 10 tr/min, 60 mV : %ld -> %ld (plafond 59)\n", (long)a20, (long)b20, (long)a10, (long)b10);
    CHECK(a20 < 59 && b20 == 59, "20 tr/min : %ld sous le plafond avant, x 2 ecrete a 59 apres (%ld)", (long)a20, (long)b20);
    CHECK(b10 < 59 && b10 >= 4 * a10, "10 tr/min : x 4 sous le plafond (%ld contre %ld)", (long)b10, (long)a10);
    CHECK(consigne(0, 400, 63, 1, 59) == 0, "cadence 0 : consigne 0 (cadence^(1/11) = 0)");

    /* 5. incident du 30/10 (perf-niveau5-12h21) : pedale relachee (742 mV pour un zero de 738), couple filtre perime
          (100 mV), niveau 5 (profil 110), plafond 98. Avant : plancher sur la cadence du pedalier -> 98. Apres : rien. */
    {
        int32_t brute11 = consigne(11, 100, 110, 0, 1000), brute24 = consigne(24, 104, 110, 0, 1000);
        int32_t avant11 = consigne(11, 100, 110, 1, 98);
        int32_t apres11 = loi_osf_plancher_sur(brute11, 11, 243, 742, 738); if (apres11 > 98) apres11 = 98;
        int32_t apres24 = loi_osf_plancher_sur(brute24, 24, 284, 740, 738); if (apres24 > 98) apres24 = 98;
        printf("incident : cadence 11, erps 243 -> avant %ld, apres %ld ; cadence 24, erps 284 -> apres %ld (loi seule %ld)\n",
               (long)avant11, (long)apres11, (long)apres24, (long)brute24);
        CHECK(avant11 == 98, "incident rejoue : l'ancien plancher donnait le plafond (%ld)", (long)avant11);
        CHECK(apres11 == brute11 && apres11 < 35, "cadence 11 sans appui : pas de plancher (%ld)", (long)apres11);
        CHECK(apres24 == brute24, "cadence 24, moteur a 64 tr/min equivalents : pas de plancher (%ld)", (long)apres24);
        /* appui reel mais moteur rapide (roue libre du pedalier) : pas d'amplification */
        int32_t b = consigne(15, 300, 110, 0, 1000);
        CHECK(loi_osf_plancher_sur(b, 15, 284, 1200, 738) == b, "appui, pedalier lent, moteur rapide : pas de plancher");
        /* vrai demarrage : pedalier et moteur lents, appui fort -> plancher x 2 */
        int32_t d = consigne(20, 250, 60, 0, 1000);
        CHECK(loi_osf_plancher_sur(d, 20, 88, 1200, 738) == loi_osf_plancher(d, 20), "demarrage : plancher x 2 conserve");
        CHECK(loi_osf_plancher_sur(d, 20, 88, 768, 738) == d, "appui de 30 mV (seuil) : pas de plancher");
        CHECK(loi_osf_plancher_sur(d, 20, 88, 769, 738) == loi_osf_plancher(d, 20), "appui de 31 mV : plancher");
    }

    printf("== %d verifications OK, %d ECHECS -> %s\n", n_ok, n_ko, n_ko ? "ROUGE" : "VERT");
    return n_ko ? 1 : 0;
}
