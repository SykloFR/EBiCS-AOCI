/*
 * Test hote de la rampe de consigne au reveil de MOE (inc/rampe_moe.h, l'en-tete reel compile dans le firmware).
 *   gcc -std=gnu99 -Wall -Wextra -Iinc tools/test_rampe_moe.c -o test_rampe_moe.exe
 * Points demandes (t = 0, 1, 200, 400, 401 ticks), symetrie, puis montee d'une consigne de 6 pts au reveil.
 */
#include <stdio.h>
#include "rampe_moe.h"

static int n_ok = 0, n_ko = 0;
#define CHECK(cond, ...) do { if (cond) { n_ok++; } else { n_ko++; printf("  ECHEC : "); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void)
{
    const int32_t L = 30;                 /* MP.phase_current_max du build route (pts) */

    /* 1. plafond */
    CHECK(rampe_moe_plafond(0u, L) == 0, "plafond t = 0 -> 0");
    CHECK(rampe_moe_plafond(1u, L) == 0, "plafond t = 1 -> 0 (0,075 tronque)");
    CHECK(rampe_moe_plafond(200u, L) == 15, "plafond t = 200 -> 15");
    CHECK(rampe_moe_plafond(400u, L) == 30, "plafond t = 400 -> 30");
    CHECK(rampe_moe_plafond(401u, L) == 30, "plafond t = 401 -> 30");

    /* 2. consigne plafonnee */
    CHECK(rampe_moe_applique(20, 0u, L) == 0, "20 a t = 0 -> 0");
    CHECK(rampe_moe_applique(20, 1u, L) == 0, "20 a t = 1 -> 0");
    CHECK(rampe_moe_applique(20, 200u, L) == 15, "20 a t = 200 -> 15");
    CHECK(rampe_moe_applique(20, 400u, L) == 20, "20 a t = 400 -> 20 (rampe terminee)");
    CHECK(rampe_moe_applique(45, 400u, L) == 45 && rampe_moe_applique(45, 401u, L) == 45,
          "rampe terminee : consigne inchangee, meme au-dessus de la limite");
    CHECK(rampe_moe_applique(10, 200u, L) == 10, "consigne sous le plafond inchangee");
    CHECK(rampe_moe_applique(0, 100u, L) == 0, "consigne nulle reste nulle");
    CHECK(rampe_moe_applique(-20, 200u, L) == -15 && rampe_moe_applique(-20, 0u, L) == 0, "symetrie : -20 a 200 -> -15");
    CHECK(rampe_moe_applique(-20, 401u, L) == -20, "symetrie : rampe terminee");
    CHECK(rampe_moe_applique(20, 0xFFFFFFFFu, L) == 20, "t tres grand : inchangee");

    /* 3. monotonie et montee d'une consigne de 6 pts au reveil */
    {
        int mono = 1, t6 = -1;
        int32_t prec = 0;
        for (uint32_t t = 0; t <= 500u; t++) {
            int32_t p = rampe_moe_plafond(t, L);
            if (p < prec) mono = 0;
            prec = p;
            if (t6 < 0 && rampe_moe_applique(6, t, L) == 6) t6 = (int)t;
        }
        printf("consigne de 6 pts au reveil : atteinte a %d ticks (%.1f ms)\n", t6, t6 / 4.0);
        CHECK(mono, "plafond croissant");
        CHECK(t6 == 80, "6 pts atteints a 80 ticks (%d)", t6);
    }

    printf("== %d verifications OK, %d ECHECS -> %s\n", n_ok, n_ko, n_ko ? "ROUGE" : "VERT");
    return n_ko ? 1 : 0;
}
