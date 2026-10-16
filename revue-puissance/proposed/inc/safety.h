/*
 * safety.h : coupure d'urgence de l'etage de puissance et verrou de defaut.
 *
 * PROPOSITION POUR REVUE (revue-puissance/PROPOSITIONS.md, points P2, P3, P10). Non flashee, non validee sur moteur.
 *
 * Principe, repris du firmware AOCI d'origine (analyse-commande-moteur.md : "Arret sur : MOE coupe puis reset" et
 * "defaut : MOE coupe, attente de disparition des defauts") :
 *   - la coupure se fait par ECRITURE DIRECTE du bit MOE (TIMER_CCHP_POEN) : aucune dependance a une fonction de bibliotheque,
 *     appelable depuis n'importe quelle interruption ou handler de faute ;
 *   - un defaut est VERROUILLE : le moteur ne repart pas tant que la carte n'a pas ete remise sous tension ;
 *   - aucune boucle infinie dans une interruption : l'interruption continue de tourner, sorties coupees.
 */
#ifndef SAFETY_H_
#define SAFETY_H_

#include "gd32f30x.h"
#include <stdint.h>

/* codes alignes sur les codes de clignotement de la LED du firmware AOCI (PB12) */
#define FAULT_NONE            0
#define FAULT_OVERCURRENT     3
#define FAULT_HALL            6
#define FAULT_VOLTAGE         8
#define FAULT_OFFSET          10   /* offset de courant hors plage, ou arret d'urgence */
#define FAULT_TEMP_SENSOR     15   /* sonde de temperature absente ou hors plage (defaut 15 de l'origine) */
#define FAULT_STARTUP_ADC     20   /* nouveau : declenchement ADC absent au demarrage */
#define FAULT_CPU_EXCEPTION   21   /* nouveau : HardFault / BusFault / UsageFault / MemManage / NMI */

extern volatile uint8_t safety_fault_code_latched;

/* coupe les sorties de puissance (MOE = 0), immediatement, sans appel de fonction de bibliotheque */
static inline void safety_pwm_off(void)
{
    TIMER_CCHP(TIMER0) &= ~(uint32_t)TIMER_CCHP_POEN;
}

void safety_fault(uint8_t code);                 /* coupe + verrouille (le premier code est conserve) */
static inline uint8_t safety_is_latched(void) { return safety_fault_code_latched != FAULT_NONE; }

/* a appeler a chaque periode PWM sur les courants de phase bruts (points ADC, offset deja retranche) */
void safety_check_phase_currents(int16_t i1, int16_t i2, int16_t i3);

#endif /* SAFETY_H_ */
