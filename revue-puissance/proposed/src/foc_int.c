/*
 * foc_int.c : PI et racine carree en entiers (voir foc_int.h).
 *
 * Equivalent du PI_control() d'EBiCS (src/FOC.c) :
 *     delta = consigne - mesure
 *     p = delta * Kp ; integrale += delta * Ki, bornee a +-limit_i
 *     cible = p + integrale ; sortie limitee en pente (max_step) puis bornee a +-limit_out
 * Differences voulues :
 *   - tout en entiers (Q16) ;
 *   - delta sature pour que gain * delta ne depasse jamais 2^31 (pas de debordement silencieux).
 */
#include "foc_int.h"

#define PI_Q 16

/* troncature vers zero comme la conversion flottant -> entier du C */
static inline int32_t q16_trunc(int32_t v)
{
    return (v >= 0) ? (v >> PI_Q) : -((-v) >> PI_Q);
}

void PI_int_init(PI_int_t *pi, int32_t gain_p_q16, int32_t gain_i_q16, int32_t limit_i, int32_t limit_out, int32_t max_step)
{
    pi->gain_p = gain_p_q16;
    pi->gain_i = gain_i_q16;
    pi->integral = 0;
    pi->limit_i = limit_i;
    pi->limit_out = limit_out;
    pi->max_step = max_step;
    pi->out = 0;
}

int32_t PI_int_control(PI_int_t *pi, int32_t setpoint, int32_t recent_value)
{
    /* delta sature : |delta| * max(Kp, Ki) < 2^30, donc p + integrale reste < 2^31 */
    int32_t gmax = (pi->gain_p > pi->gain_i) ? pi->gain_p : pi->gain_i;
    int32_t dmax = (gmax > 0) ? (0x3FFFFFFF / gmax) : 0x3FFFFFFF;
    int32_t delta = setpoint - recent_value;
    if (delta > dmax) delta = dmax;
    if (delta < -dmax) delta = -dmax;

    int32_t p_q16 = delta * pi->gain_p;
    int32_t lim_q16 = pi->limit_i << PI_Q;
    pi->integral += delta * pi->gain_i;
    if (pi->integral > lim_q16) pi->integral = lim_q16;
    if (pi->integral < -lim_q16) pi->integral = -lim_q16;

    int32_t target = q16_trunc(p_q16 + pi->integral);

    if (target > pi->out + pi->max_step) pi->out += pi->max_step;
    else if (target < pi->out - pi->max_step) pi->out -= pi->max_step;
    else pi->out = target;

    if (pi->out > pi->limit_out) pi->out = pi->limit_out;
    if (pi->out < -pi->limit_out) pi->out = -pi->limit_out;
    return pi->out;
}

/* racine carree entiere (arrondie vers le bas), methode bit a bit, au plus 16 iterations, sans division */
uint32_t isqrt_u32(uint32_t x)
{
    uint32_t res = 0;
    uint32_t bit = 1UL << 30;
    while (bit > x) bit >>= 2;
    while (bit) {
        if (x >= res + bit) {
            x -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return res;
}
