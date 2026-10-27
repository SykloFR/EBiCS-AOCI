/*
 * Test hote de la bride de vitesse du walk de banc (inc/bench_walk_bride.h, l'en-tete reel compile dans le firmware).
 *   gcc -std=gnu99 -Wall -Wextra -Iinc tools/test_bench_walk_bride.c -o test_bride.exe
 * Decision de revue du 27/10/2026 : 179 / 180 / 200 / 201 erps ; plus l'erps fige d'un rotor arrete (redemarrage).
 */
#include <stdio.h>
#include "bench_walk_bride.h"

#define SIXSTEPTHRESHOLD 10000u   /* inc/config.h */
static int n_ok = 0, n_ko = 0;
#define CHECK(cond, ...) do { if (cond) { n_ok++; } else { n_ko++; printf("  ECHEC : "); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void)
{
    const int32_t iq = 11;
    printf("== bride du walk de banc : rampe %u -> %u erps\n", BRIDE_ERPS_DEBUT, BRIDE_ERPS_FIN);
    struct { uint32_t erps; int32_t attendu; } cas[] = { {0, 11}, {179, 11}, {180, 11}, {190, 5}, {199, 0}, {200, 0}, {201, 0},
                                                         {1000, 0} };
    for (unsigned k = 0; k < sizeof cas / sizeof cas[0]; k++) {
        int32_t v = bench_walk_bride(iq, cas[k].erps);
        printf("    erps %4u : consigne %d (attendu %d)\n", (unsigned)cas[k].erps, (int)v, (int)cas[k].attendu);
        CHECK(v == cas[k].attendu, "erps %u : %d au lieu de %d", (unsigned)cas[k].erps, (int)v, (int)cas[k].attendu);
    }
    /* monotonie et bornes sur toute la plage */
    int32_t prec = iq;
    for (uint32_t e = 0; e <= 400; e++) {
        int32_t v = bench_walk_bride(iq, e);
        CHECK(v >= 0 && v <= iq && v <= prec, "erps %u : %d hors bornes ou non decroissant", (unsigned)e, (int)v);
        prec = v;
    }
    /* consigne negative (sens) : la bride ramene vers 0 sans changer de signe */
    CHECK(bench_walk_bride(-11, 190) == -5 && bench_walk_bride(-11, 201) == 0, "consigne negative mal bridee");
    /* erps fige d'un rotor arrete : la bride ne doit pas bloquer le redemarrage */
    uint32_t e1 = bench_erps_bride(230, SIXSTEPTHRESHOLD << 1, SIXSTEPTHRESHOLD);
    uint32_t e2 = bench_erps_bride(230, 416, SIXSTEPTHRESHOLD);   /* 500000 / (200 x 6) = 416 tics a 200 erps */
    printf("    erps fige 230, rotor arrete : erps retenu %u -> consigne %d ; en rotation (416 tics) : %u -> %d\n",
           (unsigned)e1, (int)bench_walk_bride(iq, e1), (unsigned)e2, (int)bench_walk_bride(iq, e2));
    CHECK(e1 == 0 && bench_walk_bride(iq, e1) == iq, "rotor arrete : redemarrage bloque");
    CHECK(e2 == 230 && bench_walk_bride(iq, e2) == 0, "en rotation a 230 erps : bride inactive");
    printf("== %d verifications OK, %d ECHECS -> %s\n", n_ok, n_ko, n_ko ? "ROUGE" : "VERT");
    return n_ko ? 1 : 0;
}
