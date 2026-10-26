/*
 * bench_can.h : télémétrie CAN de banc (porte 4, livrable B), build de banc seulement (BENCH_BUILD).
 *
 * T1 : télémétrie (émission). T2 : commandes du PC de banc, lues dans la boucle principale (aucune interruption) :
 *   0x1DF0E000 ESTOP (défaut 31 verrouillé), 0x1DF0E001 HEARTBEAT (20 Hz attendus), 0x1DF0E002 CMD [opcode, cmd_seq, ...] :
 *   0x10 OL_SET (angle u16 = deg × 65536 / 360, u_d u8) lance ou règle l'outil vecteur fixe, 0x11 OL_STOP.
 *   Réponse E206 : seq, opcode, cmd_seq, statut. Voir bench_can.c pour les règles de sécurité.
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
