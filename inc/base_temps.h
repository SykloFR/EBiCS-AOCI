/*
 * Base de temps reelle des taches lentes (build route, lot A, 28/10/2026). reg_ADC_processing est appele 1985 fois par
 * seconde PWM actif au lieu de 4000 (E212, 63c91f1) : compter un tick par appel faussait cadence, vitesse et delais d'un
 * facteur 2. Les compteurs avancent de l'ecart reel du compteur 32 bits de TIMER1 (4 kHz) entre deux appels.
 * Fonctions pures, testees par tools/test_base_temps.c.
 */
#ifndef BASE_TEMPS_H_
#define BASE_TEMPS_H_

#include <stdint.h>

#define BT_ECART_MAX 400u      /* 100 ms : au-dela l'ecart est absurde (premier appel, boucle bloquee), borne */
#define BT_SAT       64000u    /* saturation des compteurs, comme EBiCS (if(c<64000)c++) */

/* ticks de 4 kHz ecoules depuis l'appel precedent (modulo 2^32 : le debordement du compteur est transparent), bornes */
static inline uint16_t bt_ecart(uint32_t *prec, uint32_t maintenant)
{
    uint32_t e = maintenant - *prec;
    *prec = maintenant;
    return (uint16_t)(e > BT_ECART_MAX ? BT_ECART_MAX : e);
}

/* compteur avance de dt ticks, sature a BT_SAT */
static inline uint16_t bt_avance(uint16_t c, uint16_t dt)
{
    uint32_t s = (uint32_t)c + dt;
    return (uint16_t)(s > BT_SAT ? BT_SAT : s);
}

#endif /* BASE_TEMPS_H_ */
