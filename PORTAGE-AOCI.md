# Portage EBiCS M820 vers le contrôleur AOCI : journal

Base : `EBiCS/BAFANG_GD32F303RCT6`, branche `M820`, commit 4e67266 (22/07/2026). Licence GPL-3 : sources à publier si le firmware est diffusé.
Méthode et portes : `../aoci-firmware-re/methode-validation-avant-firmware.md`. Porte en cours : **3 (sans puissance)**.

## Étape 1 : compilation de référence (16/10/2026) ✅

- `Makefile` ajouté (le dépôt n'avait qu'un projet Eclipse) : `mingw32-make`, chaîne `arm-none-eabi-gcc 10.3`.
- Cible d'origine inchangée (GD32F303, Cortex-M4F) : 17,6 Ko de code, **image à 0x08005000**, VTOR à 0x5000 (`BOOTLOADER 820`). Le bootloader AOCI (0x08000000-0x08004FFF) n'est pas touché.

## Points à traiter pour la cible AOCI (APM32F103RC, Cortex-M3 sans FPU)

1. **Cœur et bibliothèque** : passer en `-mcpu=cortex-m3`, sans FPU ; remplacer la bibliothèque GD32F30x par l'équivalent F103 (registres compatibles STM32F103) ; CMSIS-DSP M3.
2. **Calcul flottant** dans l'interruption rapide (`sqrtf`, PI en `float`) : à passer en entier, sinon trop lent sans FPU (voir `etude-portage-ebics.md`).
3. **Édition de liens** : limiter la zone appli à **0x08005000-0x0803AFBF** et écrire le **CRC32 STM32 à 0x0803AFC0** après compilation, sinon le bootloader AOCI refuse de lancer l'appli. Revoir l'EEPROM virtuelle (0x0803F000) et les zones de données AOCI (0x0803B080 angles Hall, 0x0803D800 config).
4. **Brochage** : quasi identique au M820 (relevé dans `../aoci-firmware-re/brochage.md`), à vérifier broche par broche.
5. **CAN** : nœuds AOCI 0x1x au lieu de Bafang 0x0x.
6. **Mode « offroad » conservé** (décision Yanni 16/10) : déjà utilisé dans le firmware Syklo comme limiteur de vitesse par mode. Garder la limite de 25 km/h par défaut au démarrage pour la conformité EN 15194.

## Étape 2a : essai Cortex-M3 (16/10/2026)

- `make CORE=m3` : échec attendu dans `FOC.c` (instructions DSP `qadd` / `qsub` propres au M4). Cause : `Firmware/CMSIS/arm_math.h` force `ARM_MATH_CM4` (ligne 317) et `gd32f30x.h` inclut `core_cm4.h`. Pas de `core_cm3.h` ni de CMSIS-DSP M3 dans le dépôt.
- Conclusion : il faut changer **toute la couche matérielle**, pas seulement des options de compilation.
- Choix de bibliothèque (licence compatible GPL-3 exigée) :
  - SDK officiel Geehy APM32F10x : licence propriétaire Geehy, **écarté** (incompatible avec la publication GPL-3) ;
  - **GigaDevice GD32F10x Firmware Library** (BSD-3, même API que la GD32F30x d'EBiCS, puces compatibles registre à registre avec STM32F103 / APM32F103) : **recommandé**, modifications du code EBiCS minimales ;
  - CMSIS 5 (Apache-2.0) pour `core_cm3.h` et les sources CMSIS-DSP compilées pour M3 (`arm_sin_cos_q31`, Park, Clarke).

## Étape 2b : bascule sur GD32F10x + CMSIS 5 (16/10/2026) ✅ compile

- `Firmware_F10x/` : GigaDevice GD32F10x Firmware Library (commit 23a80f9, BSD-3) + CMSIS 5.3.0 (Apache-2.0) : `core_cm3.h`, `cmsis_*.h`, `arm_math.h` V1.5.3 (même version qu'EBiCS, sans le `ARM_MATH_CM4` forcé), sources DSP `arm_sin_cos_q31.c` et `arm_common_tables.c`.
- `port_f10x/` : couche de compatibilité, **sources EBiCS non modifiées** :
  - `gd32f30x.h` → inclut `gd32f10x.h`, traduit `GPIO_OSPEED_MAX` en 50 MHz ;
  - `gd32f10x_libopt.h` ;
  - `startup_gd32f10x_hd.S` (démarrage EBiCS, `.cpu cortex-m3`, `RCU_IRQHandler`) ;
  - `aoci_flash.ld` : appli **0x08005000 + 0x35FC0** (s'arrête juste avant le CRC lu par le bootloader à 0x0803AFC0).
- `mingw32-make CORE=m3` : 19,2 Ko, ARMv7-M sans FPU (flottants émulés), image à 0x08005000. La cible d'origine (`mingw32-make`) compile toujours.
- Avertissements restants : ceux d'EBiCS d'origine (dont `parser.c:137` comportement indéfini, à corriger plus tard).

**Ce binaire ne doit PAS être flashé** : il compile, mais l'horloge et les temps ne sont pas encore adaptés.

## Étape 3 (suivante) : horloge et temps

- `system_gd32f10x.c` suppose un quartz 8 MHz et 108 MHz ; la carte AOCI a un **quartz 12 MHz**, et l'APM32F103 est donné pour 96 MHz max. Cible proposée : 72 MHz (12 × 6), APB1 36 MHz.
- Constantes EBiCS calculées pour 120 MHz à revoir : période MLI `_T` (16 kHz), prescaler CAN (24 à 60 MHz → 250 kbit/s), horloge ADC (`RCU_CKADC_CKAPB2_DIV6`), TIMER1 / TIMER2 (prescalers 2 / période 9999), SysTick.
- Puis : flottants de l'interruption rapide, CRC après compilation, brochage, nœuds CAN AOCI.
