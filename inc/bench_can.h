/*
 * bench_can.h : télémétrie CAN de banc (porte 4, livrable B), build de banc seulement (BENCH_BUILD).
 *
 * Émission seule dans cette étape (T1) : aucune commande reçue, aucun effet sur la commande moteur.
 * Nœud source 0x1E (contrôleur en mode banc), cible 0x1D (PC de banc), op 0 : ID = 0x1EE8 << 16 | index << 8 | sub.
 * Priorité la plus basse du bus (11 bits de tête 0x77C) ; ni 0x12, ni 0x13, ni 0x1F : le bootloader et l'afficheur
 * ne les visent pas. Octet 0 = numéro de séquence par trame, entiers en petit-boutiste.
 * Envoi hors ISR, depuis la boucle principale, une trame par appel au plus et seulement si 2 boîtes d'émission sont
 * libres (sinon lost++, sans attente). Spécification et décodage : tools/bench/bench_can_decode.py.
 */
#ifndef BENCH_CAN_H_
#define BENCH_CAN_H_

#include <stdint.h>

#ifdef BENCH_BUILD
#define BENCH_CAN_VERSION 1u

extern volatile uint32_t bench_ms;                  /* incrémenté par SysTick (1 kHz) */

void bench_can_init(void);                          /* après can_networking_init() */
void bench_can_sample(int16_t i1, int16_t i2, int16_t i3);  /* 4 kHz, boucle principale (reg_ADC_processing) */
void bench_can_poll(void);                          /* à chaque tour de la boucle principale */
#endif

#endif /* BENCH_CAN_H_ */
