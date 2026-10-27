/*
 * Test hote du choix de variante (inc/variante.h, l'en-tete reel compile dans le firmware).
 *   gcc -std=gnu99 -Wall -Wextra -Iinc tools/test_variante.c -o test_variante.exe
 * Les 256 valeurs d'octet, sans repli (banc, production) et avec repli 2 (build route).
 */
#include <stdio.h>
#include "variante.h"

static int n_ok = 0, n_ko = 0;
#define CHECK(cond, ...) do { if (cond) { n_ok++; } else { n_ko++; printf("  ECHEC : "); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void)
{
    for (int r = 0; r <= 2; r += 2) {                          /* 0 : sans repli ; 2 : repli du build route */
        int acceptees = 0;
        for (int lu = 0; lu < 256; lu++) {
            uint8_t rep = 9;
            uint8_t v = variante_effective((uint8_t)lu, (uint8_t)r, &rep);
            uint8_t attendu = (lu == 1 || lu == 2) ? (uint8_t)lu : ((lu == 0xFF && r == 2) ? 2u : 0u);
            uint8_t rep_att = (lu == 0xFF && r == 2) ? 1u : 0u;
            CHECK(v == attendu && rep == rep_att, "repli %d, octet 0x%02X : variante %u (repli %u) au lieu de %u (%u)",
                  r, lu, v, rep, attendu, rep_att);
            if (v) acceptees++;
        }
        printf("repli %d : octets acceptes %d / 256 (1, 2%s)\n", r, acceptees, r ? ", 0xFF -> 2" : "");
    }
    printf("== %d verifications OK, %d ECHECS -> %s\n", n_ok, n_ko, n_ko ? "ROUGE" : "VERT");
    return n_ko ? 1 : 0;
}
