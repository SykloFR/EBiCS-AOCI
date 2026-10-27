/*
 * Maintien de MOE tant que le moteur tourne (build route, 27/10/2026). Couper MOE sur consigne nulle puis relancer le pont
 * avec le PI a zero, moteur lance, fait debiter la force contre-electromotrice (i_q -73 pts, Vbat +0,4 V, 63c91f1).
 * Comme EBiCS d'origine, le pont n'est coupe qu'a l'arret ; arret = plus de ROUTE_ARRET_TICKS ticks de TIMER1 (4 kHz reels)
 * sans front Hall. Fonctions pures, testees par tools/test_moe_rotation.c.
 */
#ifndef MOE_ROTATION_H_
#define MOE_ROTATION_H_

#include <stdint.h>

#define ROUTE_ARRET_TICKS 400u    /* 100 ms sans front Hall : erps < 1,7 */
#define ROUTE_TICKS_SAT   64000u

/* vrai si MOE doit etre coupe : consigne nulle, pont actif et moteur arrete */
static inline uint8_t route_coupe_moe(int32_t consigne, uint8_t pwm_on, uint16_t ticks_sans_hall)
{
    return (consigne == 0 && pwm_on && ticks_sans_hall > ROUTE_ARRET_TICKS) ? 1u : 0u;
}

/* vrai si les integrales du PI peuvent etre remises a zero : seulement pont coupe (u_q ne chute pas en rotation) */
static inline uint8_t route_raz_pi_autorisee(uint8_t pwm_on)
{
    return pwm_on ? 0u : 1u;
}

#endif /* MOE_ROTATION_H_ */
