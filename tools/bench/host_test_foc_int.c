/*
 * host_test_foc_int.c : test sur PC (gcc hote) de revue-puissance/proposed/src/foc_int.c
 *   1. isqrt_u32 : comparaison exhaustive avec floor(sqrt) pour x < 2^25, puis 20 millions de tirages sur 32 bits ;
 *   2. PI en entiers contre le PI flottant d'EBiCS (copie de PI_control, src/FOC.c) sur des sequences aleatoires
 *      realistes (gains P=1,5 I=0,01, limites 1920, pas 15) : ecart maximal de la sortie.
 * Compilation : gcc -O2 -I../../revue-puissance/proposed/inc host_test_foc_int.c ../../revue-puissance/proposed/src/foc_int.c -lm
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include "foc_int.h"

/* ---- copie du PI flottant d'EBiCS (src/FOC.c, PI_control), champs reduits ---- */
typedef struct { float gain_p, gain_i; int16_t limit_i, limit_output; int16_t recent_value; int32_t setpoint; float integral_part; int16_t max_step; int32_t out; } PI_f_t;
static int32_t PI_f(PI_f_t *PI_c)
{
    float Delta = (PI_c->setpoint - PI_c->recent_value);
    float p_part = Delta * PI_c->gain_p;
    PI_c->integral_part += Delta * PI_c->gain_i;
    if (PI_c->integral_part > PI_c->limit_i) PI_c->integral_part = PI_c->limit_i;
    if (PI_c->integral_part < -(PI_c->limit_i)) PI_c->integral_part = -(PI_c->limit_i);
    if (p_part + PI_c->integral_part > PI_c->out + PI_c->max_step) PI_c->out += PI_c->max_step;
    else if (p_part + PI_c->integral_part < PI_c->out - PI_c->max_step) PI_c->out -= PI_c->max_step;
    else PI_c->out = (p_part + PI_c->integral_part);
    if (PI_c->out > PI_c->limit_output) PI_c->out = PI_c->limit_output;
    if (PI_c->out < -(PI_c->limit_output)) PI_c->out = -(PI_c->limit_output);
    return PI_c->out;
}

static uint32_t rng = 12345;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

int main(void)
{
    /* 1. racine carree */
    uint32_t bad = 0;
    for (uint32_t x = 0; x < (1u << 25); x++) {
        uint32_t r = isqrt_u32(x);
        if (r * r > x || (r + 1) * (uint64_t)(r + 1) <= x) { if (bad++ < 5) printf("isqrt faux pour %u -> %u\n", x, r); }
    }
    for (int i = 0; i < 20000000; i++) {
        uint32_t x = rnd();
        uint32_t r = isqrt_u32(x);
        if ((uint64_t)r * r > x || (uint64_t)(r + 1) * (r + 1) <= x) { if (bad++ < 5) printf("isqrt faux pour %u -> %u\n", x, r); }
    }
    printf("isqrt_u32 : %s (%u erreurs)\n", bad ? "ECHEC" : "OK", bad);

    /* ecart avec (int32_t)sqrtf((float)x) pour les valeurs reellement utilisees (u_d^2 + u_q^2 <= 2 * 1920^2) */
    int diff_sqrtf = 0;
    for (int32_t x = 0; x <= 2 * 1920 * 1920; x++) {
        if ((uint32_t)(int32_t)sqrtf((float)x) != isqrt_u32((uint32_t)x)) diff_sqrtf++;
    }
    printf("isqrt_u32 contre (int)sqrtf((float)x), x <= 2*1920^2 : %d valeurs differentes\n", diff_sqrtf);

    /* 2. PI : 200 sequences de 20000 pas */
    int worst = 0; long long sum_abs = 0, n = 0;
    for (int seq = 0; seq < 200; seq++) {
        PI_f_t pf = { 1.5f, 0.01f, 1920, 1920, 0, 0, 0.0f, 15, 0 };
        PI_int_t pi; PI_int_init(&pi, Q16(1.5), Q16(0.01), 1920, 1920, 15);
        int32_t sp = 0, meas = 0;
        for (int k = 0; k < 20000; k++) {
            if ((rnd() & 0x3FF) == 0) sp = (int32_t)(rnd() % 1501) - 300;          /* consigne par paliers */
            meas += ((sp - meas) >> 4) + (int32_t)(rnd() % 7) - 3;                  /* mesure qui suit avec du bruit */
            if ((rnd() & 0xFFF) == 0) meas = (int32_t)(rnd() % 4001) - 2000;        /* a-coups */
            pf.setpoint = sp; pf.recent_value = (int16_t)meas;
            int32_t a = PI_f(&pf);
            int32_t b = PI_int_control(&pi, sp, meas);
            int d = abs(a - b);
            if (d > worst) worst = d;
            sum_abs += d; n++;
        }
    }
    printf("PI entier contre PI flottant : ecart maximal %d point(s), moyen %.4f point sur %lld pas\n", worst, (double)sum_abs / n, n);
    return (bad == 0 && worst <= 3) ? 0 : 1;
}
