/*
 * aoci_board.h : brochage du contrôleur AOCI (carte LDCKLSX12C1.2-B, APM32F103RC) pour le portage EBiCS.
 *
 * Source : aoci-firmware-re/brochage.md, confirmé par lecture en direct des registres GPIO du firmware
 * d'origine par SWD (16/10/2026, validation/porte-3/releve-gpio-live.md).
 */
#ifndef AOCI_BOARD_H_
#define AOCI_BOARD_H_

#include "gd32f30x.h"

/* Éclairage : sorties push-pull actives à l'état BAS (ODR = 1 : éteint). */
#define LIGHT_FRONT_PORT  GPIOB
#define LIGHT_FRONT_PIN   GPIO_PIN_10
#define LIGHT_REAR_PORT   GPIOA          /* PA15 = JTDI, libérée par la remap SWD seul */
#define LIGHT_REAR_PIN    GPIO_PIN_15

#endif /* AOCI_BOARD_H_ */
