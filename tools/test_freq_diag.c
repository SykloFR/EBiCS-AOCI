/*
 * Test hote de la mesure de frequence (inc/freq_diag.h, l'en-tete reel compile dans le firmware).
 *   gcc -std=gnu99 -Wall -Wextra -Iinc tools/test_freq_diag.c -o test_freq_diag.exe
 * Fenetres de 1000 ms exactes, ecarts justes au passage par 0 des compteurs 32 bits, saturation, part PWM.
 */
#include <stdio.h>
#include "freq_diag.h"

static int n_ok = 0, n_ko = 0;
#define CHECK(cond, ...) do { if (cond) { n_ok++; } else { n_ko++; printf("  ECHEC : "); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void)
{
    /* 1. 4000 debordements / s, 2700 traitements / s, 50000 tours / s, PWM 300 ms sur 1000 ; depart pres de 2^32 */
    {
        freq_diag_t f = {0};
        uint32_t isr = 0xFFFFF000u, tr = 0xFFFFF800u, bo = 0xFFFF0000u;
        f.prec_isr = isr; f.prec_trait = tr; f.prec_boucle = bo;      /* fenetre amorcee sur ces valeurs */
        double ai = 0, at = 0, ab = 0;
        for (int w = 0; w < 5; w++) {
            for (int ms = 0; ms < 1000; ms++) {
                ai += 4.0; at += 2.7; ab += 50.0;
                while (ai >= 1) { isr++; ai -= 1; }
                while (at >= 1) { tr++; at -= 1; }
                while (ab >= 1) { bo++; ab -= 1; }
                freq_diag_ms(&f, isr, tr, bo, (uint8_t)(ms < 300));
                if (ms < 999) CHECK(f.fenetres == w, "fenetre close trop tot (%d ms)", ms);
            }
            CHECK(f.fenetres == w + 1, "fenetre %d non close", w);
            CHECK(f.isr_hz == 4000 && f.trait_hz >= 2699 && f.trait_hz <= 2701 && f.boucle_hz == 50000,
                  "fenetre %d : %u %u %u", w, f.isr_hz, f.trait_hz, f.boucle_hz);
            CHECK(f.pwm_q4 == 75, "part PWM %u au lieu de 75", f.pwm_q4);
        }
        CHECK(isr == 20000u - 0x1000u && tr < 0xFFFFF800u, "les compteurs sont bien passes par 0 (%lu %lu)", (unsigned long)isr, (unsigned long)tr);
    }
    /* 2. saturation a 65535 et PWM toujours actif (250) ou jamais (0) */
    {
        freq_diag_t f = {0};
        uint32_t bo = 0;
        for (int ms = 0; ms < 1000; ms++) { bo += 100; freq_diag_ms(&f, 0, 0, bo, 1); }
        CHECK(f.boucle_hz == 65535 && f.pwm_q4 == 250 && f.isr_hz == 0, "saturation : %u pwm %u", f.boucle_hz, f.pwm_q4);
        for (int ms = 0; ms < 1000; ms++) freq_diag_ms(&f, 0, 0, bo, 0);
        CHECK(f.boucle_hz == 0 && f.pwm_q4 == 0 && f.fenetres == 2, "repos : %u pwm %u", f.boucle_hz, f.pwm_q4);
    }
    printf("== %d verifications OK, %d ECHECS -> %s\n", n_ok, n_ko, n_ko ? "ROUGE" : "VERT");
    return n_ko ? 1 : 0;
}
