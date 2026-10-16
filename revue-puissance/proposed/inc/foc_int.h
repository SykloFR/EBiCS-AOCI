/*
 * foc_int.h : regulateur PI et racine carree en entiers, pour le Cortex-M3 sans FPU du controleur AOCI.
 *
 * PROPOSITION POUR REVUE (revue-puissance/PROPOSITIONS.md, point P4). Non flashee, non validee sur moteur.
 *
 * Pourquoi : EBiCS (GD32F303, Cortex-M4F) fait le PI et la limitation de cercle en flottant dans l'interruption
 * rapide (16,5 kHz, 4000 cycles disponibles a 66 MHz). Sur un M3 le flottant est emule par logiciel.
 * Le meme calcul en entiers (gains en Q16) donne le meme resultat a +-1 point pres (essai sur PC,
 * tools/bench/host_test_foc_int.c) et coute un ordre de grandeur de moins (mesure sur la carte,
 * tools/bench/t5_ram_cost.py).
 */
#ifndef FOC_INT_H_
#define FOC_INT_H_

#include <stdint.h>

/* Conversion d'une constante de gain en Q16 a la compilation (aucun flottant a l'execution) */
#define Q16(x) ((int32_t)((x) * 65536.0 + 0.5))

typedef struct {
    int32_t gain_p;        /* Q16 */
    int32_t gain_i;        /* Q16 */
    int32_t integral;      /* Q16 */
    int32_t limit_i;       /* en points (la limite de l'integrale, comme limit_i d'EBiCS) */
    int32_t limit_out;     /* en points */
    int32_t max_step;      /* variation maximale de la sortie par appel */
    int32_t out;           /* sortie, en points */
} PI_int_t;

void     PI_int_init(PI_int_t *pi, int32_t gain_p_q16, int32_t gain_i_q16, int32_t limit_i, int32_t limit_out, int32_t max_step);
int32_t  PI_int_control(PI_int_t *pi, int32_t setpoint, int32_t recent_value);
uint32_t isqrt_u32(uint32_t x);

#endif /* FOC_INT_H_ */
