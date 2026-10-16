/*
 * ramtest_cost.c : cout en cycles, sur le vrai Cortex-M3 de la carte, du calcul "PI x2 + limitation de cercle"
 * de l'interruption rapide (runPIcontrol() d'EBiCS), en flottant (code d'origine) et en entiers (proposition P4).
 * Calcul pur : aucune broche, aucun timer, aucune ecriture flash. Budget a 66 MHz / 16,5 kHz : 4000 cycles par periode.
 */
#include "gd32f10x.h"
#include <math.h>
#include "foc_int.h"

#define RES_ADDR 0x2000B000U
typedef struct { uint32_t magic, sysclk; uint32_t f_min, f_max, f_sum, i_min, i_max, i_sum, n, chk_f, chk_i; } result_t;
#define R ((volatile result_t *)RES_ADDR)

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

#define U_MAX 1920L

/* version d'origine (runPIcontrol, parties PI + limitation de cercle) */
static int32_t step_float(PI_f_t *iq, PI_f_t *id, int32_t i_q, int32_t i_d, int32_t sp_q, int32_t *u_d, int32_t *u_q)
{
    iq->recent_value = i_q; iq->setpoint = sp_q;
    int32_t uq = PI_f(iq);
    id->recent_value = i_d; id->setpoint = 0;
    int32_t ud = -PI_f(id);
    int32_t u_abs = (int32_t)sqrtf((float)(ud * ud + uq * uq));
    if (u_abs > U_MAX) { uq = (uq * U_MAX) / u_abs; ud = (ud * U_MAX) / u_abs; u_abs = U_MAX; }
    *u_d = ud; *u_q = uq;
    return u_abs;
}

static int32_t step_int(PI_int_t *iq, PI_int_t *id, int32_t i_q, int32_t i_d, int32_t sp_q, int32_t *u_d, int32_t *u_q)
{
    int32_t uq = PI_int_control(iq, sp_q, i_q);
    int32_t ud = -PI_int_control(id, 0, i_d);
    int32_t u_abs = (int32_t)isqrt_u32((uint32_t)(ud * ud + uq * uq));
    if (u_abs > U_MAX) { uq = (uq * U_MAX) / u_abs; ud = (ud * U_MAX) / u_abs; u_abs = U_MAX; }
    *u_d = ud; *u_q = uq;
    return u_abs;
}

static uint32_t rng = 777;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

__attribute__((section(".entry"), noreturn))
void test_start(void)
{
    __disable_irq();
    R->magic = 0;
    SystemInit();
    R->sysclk = SystemCoreClock;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    PI_f_t fq = { 1.5f, 0.01f, 1920, 1920, 0, 0, 0.0f, 15, 0 }, fd = fq;
    PI_int_t iq, id;
    PI_int_init(&iq, Q16(1.5), Q16(0.01), 1920, 1920, 15);
    PI_int_init(&id, Q16(1.5), Q16(0.01), 1800, 1920, 15);
    fd.limit_i = 1800;

    uint32_t fmin = 0xFFFFFFFF, fmax = 0, fsum = 0, imin = 0xFFFFFFFF, imax = 0, isum = 0, chk_f = 0, chk_i = 0;
    int32_t ud, uq;
    const int N = 2000;
    for (int k = 0; k < N; k++) {
        /* entrees realistes, avec des phases ou la limitation de cercle (2 divisions) est active */
        int32_t sp_q = (int32_t)(rnd() % 1801) - 300;
        int32_t i_q = (int32_t)(rnd() % 1601) - 400;
        int32_t i_d = (int32_t)(rnd() % 801) - 400;
        uint32_t t0 = DWT->CYCCNT;
        step_float(&fq, &fd, i_q, i_d, sp_q, &ud, &uq);
        uint32_t cf = DWT->CYCCNT - t0;
        chk_f += (uint32_t)(ud + uq);
        t0 = DWT->CYCCNT;
        step_int(&iq, &id, i_q, i_d, sp_q, &ud, &uq);
        uint32_t ci = DWT->CYCCNT - t0;
        chk_i += (uint32_t)(ud + uq);
        if (cf < fmin) fmin = cf; if (cf > fmax) fmax = cf; fsum += cf;
        if (ci < imin) imin = ci; if (ci > imax) imax = ci; isum += ci;
    }
    R->f_min = fmin; R->f_max = fmax; R->f_sum = fsum;
    R->i_min = imin; R->i_max = imax; R->i_sum = isum; R->n = N;
    R->chk_f = chk_f; R->chk_i = chk_i;
    R->magic = 0xA0C1A0C1U;
    __asm volatile ("bkpt #0");
    while (1) { }
}
