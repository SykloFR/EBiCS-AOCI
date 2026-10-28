/*
 * Rampe de consigne au reveil de MOE (build route, lot A, 28/10/2026). A chaque MOE = 1, la consigne transmise au PI est
 * plafonnee en valeur absolue par une rampe lineaire de 0 a la limite sur RAMPE_MOE_TICKS ticks de TIMER1 (4 kHz reels),
 * puis laissee inchangee. Fonctions pures, testees par tools/test_rampe_moe.c.
 */
#ifndef RAMPE_MOE_H_
#define RAMPE_MOE_H_

#include <stdint.h>

#define RAMPE_MOE_TICKS 400u    /* 100 ms */

/* plafond de |consigne| t ticks apres MOE = 1 */
static inline int32_t rampe_moe_plafond(uint32_t t, int32_t limite)
{
    return t >= RAMPE_MOE_TICKS ? limite : (int32_t)((int64_t)limite * (int32_t)t / (int32_t)RAMPE_MOE_TICKS);
}

/* consigne plafonnee ; rampe terminee : consigne inchangee */
static inline int32_t rampe_moe_applique(int32_t consigne, uint32_t t, int32_t limite)
{
    int32_t p;
    if (t >= RAMPE_MOE_TICKS) return consigne;
    p = rampe_moe_plafond(t, limite);
    if (consigne > p) return p;
    if (consigne < -p) return -p;
    return consigne;
}

#endif /* RAMPE_MOE_H_ */
