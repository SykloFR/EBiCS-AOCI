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
