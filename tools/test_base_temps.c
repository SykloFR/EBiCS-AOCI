/*
 * Test hote de la base de temps reelle (inc/base_temps.h, l'en-tete reel compile dans le firmware).
 *   gcc -std=gnu99 -Wall -Wextra -Iinc tools/test_base_temps.c -o test_base_temps.exe
 * Cas unitaires de bt_ecart / bt_avance, puis scenario : PD2 a 60 tr/min (48 impulsions par tour), reg_ADC appele un tick
 * sur deux (2 kHz, PWM actif) ; cadence par l'ecart reel contre cadence d'aujourd'hui (+1 par appel).
 */
#include <stdio.h>
#include "base_temps.h"
#include "pedalier_pd2.h"

static int n_ok = 0, n_ko = 0;
#define CHECK(cond, ...) do { if (cond) { n_ok++; } else { n_ko++; printf("  ECHEC : "); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* n appels, un appel tous les pas ticks a partir de t0 ; renvoie la somme des ecarts */
static uint32_t somme(uint32_t t0, uint32_t n, uint32_t pas, int *ecart_constant)
{
    uint32_t prec = t0, s = 0;
    *ecart_constant = 1;
    for (uint32_t i = 1; i <= n; i++) {
        uint16_t e = bt_ecart(&prec, t0 + i * pas);
        if (e != pas) *ecart_constant = 0;
        s += e;
    }
    return s;
}

int main(void)
{
    int k;
    uint32_t prec;

    /* 1. appels reguliers et appels manques */
    CHECK(somme(1000u, 4000u, 1u, &k) == 4000u && k, "reguliers : 4000 appels, ecart 1, somme 4000");
    CHECK(somme(1000u, 2000u, 2u, &k) == 4000u && k, "1 sur 2 : 2000 appels, ecart 2, somme 4000");
    CHECK(somme(0xFFFFF000u, 8192u, 1u, &k) == 8192u && k, "reguliers a travers le debordement du compteur");

    /* 2. rafale de 5 */
    prec = 500u;
    CHECK(bt_ecart(&prec, 505u) == 5u && prec == 505u, "5 ticks sans appel puis un appel -> 5");
    {
        static const uint16_t att[5] = {1, 0, 0, 0, 0};
        int ok = 1;
        prec = 700u;
        for (int i = 0; i < 5; i++) if (bt_ecart(&prec, 701u) != att[i]) ok = 0;
        CHECK(ok, "5 appels dans le meme tick -> 1, 0, 0, 0, 0");
    }

    /* 3. ecart nul, debordement, ecart absurde */
    prec = 1234u;
    CHECK(bt_ecart(&prec, 1234u) == 0u, "ecart nul -> 0");
    prec = 0xFFFFFFFEu;
    CHECK(bt_ecart(&prec, 3u) == 5u, "debordement 0xFFFFFFFE -> 3 : 5");
    prec = 0u;
    CHECK(bt_ecart(&prec, 0x80000000u) == BT_ECART_MAX, "ecart 0x80000000 borne a %u", BT_ECART_MAX);
    prec = 0u;
    CHECK(bt_ecart(&prec, 123456u) == BT_ECART_MAX && prec == 123456u, "premier appel a 123456 : borne, reference prise");
    prec = 1000u;
    CHECK(bt_ecart(&prec, 999u) == BT_ECART_MAX, "compteur qui recule (absurde) : borne");
    prec = 0u;
    CHECK(bt_ecart(&prec, BT_ECART_MAX) == BT_ECART_MAX && bt_ecart(&prec, 2u * BT_ECART_MAX + 1u) == BT_ECART_MAX,
          "ecart egal a la borne puis borne + 1");

    /* 4. bt_avance : saturation a 64000 comme if(c<64000)c++ */
    CHECK(bt_avance(0u, 0u) == 0u && bt_avance(10u, 2u) == 12u, "avance simple");
    CHECK(bt_avance(63999u, 1u) == 64000u && bt_avance(64000u, 1u) == 64000u && bt_avance(63990u, 400u) == 64000u,
          "saturation a 64000");
    CHECK(bt_avance(65535u, 400u) == 64000u, "pas de debordement 16 bits");
    {
        uint16_t a = 0, b = 0;
        for (int i = 0; i < 70000; i++) { a = bt_avance(a, 1u); if (b < 64000u) b++; }
        CHECK(a == b, "identique a EBiCS a un tick par appel (%u / %u)", a, b);
    }

    /* 5. scenario : PD2 a 60 tr/min, 48 impulsions par tour -> une impulsion tous les 4000 / 48 = 83,3 ticks.
          Appels de reg_ADC un tick sur deux. A chaque front, cadence = 5000 / PAS_counter puis remise a 0 (PAS_processing). */
    {
        uint32_t tick = 0x7FFFF000u, prec_t = tick;
        uint16_t pas_reel = 0, pas_appels = 0;
        double phase = 0.0;
        int n = 0, cad_min = 1000, cad_max = 0, anc_min = 1000, anc_max = 0, front = 0;
        for (int i = 0; i < 4 * 4000; i++) {
            tick++;
            phase += 48.0 / 4000.0;                               /* 60 tr/min */
            if (phase >= 1.0) { phase -= 1.0; front = 1; }
            if (tick & 1u) continue;                              /* appel manque : 2 kHz */
            uint16_t dt = bt_ecart(&prec_t, tick);
            pas_reel = bt_avance(pas_reel, dt);
            if (pas_appels < 64000u) pas_appels++;
            if (front) {
                front = 0;
                if (n++ > 2) {                                    /* premieres periodes : compteurs partis de 0 */
                    int c = pd2_cadence(pas_reel), a = pd2_cadence(pas_appels);
                    if (c < cad_min) cad_min = c;
                    if (c > cad_max) cad_max = c;
                    if (a < anc_min) anc_min = a;
                    if (a > anc_max) anc_max = a;
                }
                pas_reel = 0; pas_appels = 0;
            }
        }
        printf("scenario 60 tr/min a 2 kHz : cadence par l'ecart %d-%d, par appel %d-%d (%d fronts)\n",
               cad_min, cad_max, anc_min, anc_max, n);
        CHECK(cad_min >= 59 && cad_max <= 61, "cadence par l'ecart 60 +- 1 (%d-%d)", cad_min, cad_max);
        CHECK(anc_min >= 117 && anc_max <= 121, "cadence d'aujourd'hui 119 +- 2 (%d-%d)", anc_min, anc_max);
    }

    printf("== %d verifications OK, %d ECHECS -> %s\n", n_ok, n_ko, n_ko ? "ROUGE" : "VERT");
    return n_ko ? 1 : 0;
}
