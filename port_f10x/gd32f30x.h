/* Couche de compatibilite portage AOCI : le code EBiCS inclut gd32f30x.h,
   la cible AOCI (APM32F103RC, compatible GD32F10x HD) utilise gd32f10x.h */
#ifndef GD32F30X_COMPAT_H
#define GD32F30X_COMPAT_H
#include "gd32f10x.h"

/* GD32F30x : vitesse > 50 MHz ; le F10x plafonne a 50 MHz */
#ifndef GPIO_OSPEED_MAX
#define GPIO_OSPEED_MAX GPIO_OSPEED_50MHZ
#endif
#endif
