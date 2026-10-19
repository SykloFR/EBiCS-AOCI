# Portage EBiCS M820 vers le contrôleur AOCI : journal

Base : `EBiCS/BAFANG_GD32F303RCT6`, branche `M820`, commit 4e67266 (22/07/2026). Licence GPL-3 : sources à publier si le firmware est diffusé.
Méthode et portes : `../aoci-firmware-re/methode-validation-avant-firmware.md`. Porte en cours : **3 (sans puissance)**.

## Étape 1 : compilation de référence (16/10/2026) ✅

- `Makefile` ajouté (le dépôt n'avait qu'un projet Eclipse) : `mingw32-make`, chaîne `arm-none-eabi-gcc 10.3`.
- Cible d'origine inchangée (GD32F303, Cortex-M4F) : ~~17,6 Ko~~ (binaire creux, voir étape 4), **image à 0x08005000**, VTOR à 0x5000 (`BOOTLOADER 820`). Le bootloader AOCI (0x08000000-0x08004FFF) n'est pas touché.

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
- `mingw32-make CORE=m3` : ~~19,2 Ko~~ (binaire creux, voir étape 4 : image réelle de 34,8 Ko), ARMv7-M sans FPU (flottants émulés), image à 0x08005000. La cible d'origine (`mingw32-make`) compile toujours.
- Avertissements restants : ceux d'EBiCS d'origine (dont `parser.c:137` comportement indéfini, à corriger plus tard).

**Ce binaire ne doit PAS être flashé** : il compile, mais l'horloge et les temps ne sont pas encore adaptés.

## Étape 3 : horloge, temps, CRC (16/10/2026) : fait, non testé sur carte

Décision : **66 MHz** (quartz 12 MHz / 2 x 11), comme le firmware AOCI d'origine (relevé dans `FUN_0800a100` / `FUN_0800a174`), plutôt que 72 MHz. Repli HSI/2 x 16 = 64 MHz si le quartz ne démarre pas.

- `port_f10x/system_aoci.c` remplace `system_gd32f10x.c` (retiré de la compilation) : mêmes réglages que l'origine (AHB 66, APB2 66, APB1 33 MHz, 2 états d'attente flash), délais bornés (plus de boucle infinie), `SystemCoreClock` recalculé, VTOR à 0x5000. `-DHXTAL_VALUE=12000000U` dans le Makefile, avec `#error` si autre valeur.
- CAN (`can_networking_init`) : 250 kbit/s comme l'origine, prescaler 11 et 1+9+2 tq à 66 MHz ; repli 64 MHz : prescaler 8 et 1+13+2 tq.
- TIMER1 (base lente, 4 kHz) : prescaler 1, période `SystemCoreClock/2/4000-1` (8249).
- TIMER2 (base Hall, 500 kHz, hypothèse du calcul de vitesse) : prescaler `SystemCoreClock/500000-1` (131).
- SysTick : déjà dérivé de `SystemCoreClock`.
- `tools/make_aoci_image.py` + cible `mingw32-make CORE=m3 aoci` : complète l'image à 0xFF jusqu'à 0x0803AFBF, calcule le CRC32 STM32 et l'écrit à 0x0803AFC0, sort une image de 0x36000 octets (0x08005000-0x0803AFFF). Contrôle SP/reset. Le mode `--verify` a été validé sur `dump1.bin` (CRC 0x3CEBF07C = valeur stockée). L'image complète doit être écrite en entier : le CRC porte sur le remplissage 0xFF, l'ancienne appli (jusqu'à 0x0801BDFF) doit être effacée.
- Makefile : portable Windows (création de dossiers et `clean` sans `rm`). Cible M4 d'origine toujours compilable.

**Laissé volontairement (puissance, revue croisée Fangyan avant essai sous puissance)** : période MLI `_T` (3750 pour 16 kHz à 120 MHz ; **2000 pour 16,5 kHz à 66 MHz**, valeur de l'origine), temps mort (DTG 25 dans l'origine, ~379 ns), temps d'échantillonnage ADC injecté (55,5 cycles à 11 MHz = 6,2 µs, à réduire), `TRIGGER_OFFSET_ADC`, flottants dans l'interruption rapide, coupure surintensité, BKIN, HardFault. `rcu_adc_clock_config(RCU_CKADC_CKAPB2_DIV6)` donne 11 MHz à 66 MHz (< 14 MHz max) : inchangé.

**Toujours interdit de flasher** ce binaire : `_T` et le reste de la partie puissance sont encore ceux du GD32F303 à 120 MHz.

## Reste après l'étape 4

Voir la liste en fin de section « Étape 4 ».

## Étape 4 : cible 36 V 15 A, brochage, CAN, bug -Os (16/10/2026) : fait, non testé sur carte

**Correctif majeur : les binaires des étapes 1 à 3 étaient creux.** Compilé en `-Os`, `while(!reg_ADC_flag);` (drapeau non `volatile`, posé par une interruption) devenait une boucle infinie et GCC supprimait tout le code qui suit : `main` s'arrêtait après `parse_MOparams` sur un `b.n .`, sans boucle principale, CAN ni assistance. D'où les « 17,6 Ko » et « 19,2 Ko » annoncés plus haut. EBiCS est compilé en `-O0` dans son projet Eclipse, ce qui masque le défaut. Correction : `volatile` sur les variables partagées avec les interruptions et le DMA (`adc_value`, `receive_flag`, `PAS_flag`, `Speed_flag`, `reg_ADC_flag`, `OnOffButton_flag`, `BC_limit_flag`, `slow_loop_counter`, `PAS_counter`, `Speed_counter`, `offroadcounter`). Image réelle maintenant : **34,8 Ko**, `processCAN_Rx`, `sendCAN_Poll`, `PAS_processing`, etc. présents. Il reste à auditer les autres variables partagées avec les interruptions (voir la liste ci-dessous).

- **Cible : 36 V 15 A** (décision Yanni, c'est le firmware réellement présent sur la carte de banc). `config.h` : `SYSTEM_VOLTAGE` 36, `MAX_VOLTAGE` 42, `VOLTAGE_MIN` 1200 (30 V, coupure du firmware AOCI 36 V d'origine). **Non touchés (puissance)** : `BATTERYCURRENT_MAX` 12000 et `PH_CURRENT_MAX` 700 (valeurs M820, dépendantes de l'étalonnage du courant), `BATTERY_LEVEL_x`, `CAL_*`.
- **Brochage** relevé en direct sur le firmware d'origine (`aoci-firmware-re/validation/porte-3/releve-gpio-live.md`) : `inc/aoci_board.h` ; PB10 (feu avant) et PA15 (feu arrière / stop) sorties push-pull **actives à l'état bas**, initialisées éteintes avant de passer en sortie ; **PB6 n'est plus une sortie** (entrée flottante sur la carte AOCI) ; la remap SWD seul, PB3/PB4/PB5/PB12 en sortie et les entrées PC10/12/13, PD2, PB2 sont compatibles.
- **Second `SystemInit()` dans `main()` retiré** (le démarrage l'appelle déjà ; le second ramenait l'horloge sur le HSI).
- **CAN** : nœuds AOCI par macros (`CAN_NODE_CONTROLLER` 0x12, `CAN_NODE_DISPLAY` 0x13, `CAN_NODE_BESST` 0x15, diffusion 0x1F ; `-DCAN_NODE_BAFANG` rétablit 0x02/0x03/0x05). Trames émises 0x12F83200/01/05 (même format que l'origine AOCI). **Réception** : l'afficheur AOCI émet `0x13F86300` vers la **diffusion 0x1F**, que EBiCS (cible 2 seulement) ignorait ; elle est maintenant acceptée sans accusé ni réponse. `0x6200` (rotation moteur d'apprentissage), `0x6101` (réglages d'usine) et `0x3005` (reset logiciel vers le bootloader) sont réservés à l'outil PC (source 0x15, cible contrôleur). `PRINTDEBUG_CAN` désactivé (la trame de debug 0x7AB n'a pas sa place sur le bus afficheur).
- **EEPROM virtuelle** (0x0803F000, 1 page de 2 Ko) : la page est vide dans le dump AOCI et ne chevauche ni les angles Hall d'origine (0x0803B080), ni les blocs 0x0803D000/D800 : aucun conflit. Le bootloader AOCI n'écrit pas au-delà de 0x36010 octets depuis 0x08005000.
- `parser.c:137` : boucle `6 x 7` sur `assist_profile[5][6]` corrigée en `5 x 6` (le débordement était écrasé ensuite par l'initialisation de `assist_settings`, l'état final est identique).

### À faire ensuite (non traité)

1. Audit complet des variables partagées avec les interruptions (`MS`, compteurs, `ui8_hall_state`...) : passer en `volatile` ou revoir les sections critiques.
2. Boucles d'attente au démarrage (`while(!reg_ADC_flag)` 2000 fois) sans délai de garde : si le déclenchement ADC ne part pas, la carte reste bloquée ; chien de garde `fwdgt_config(65000, DIV256)` : le registre ne fait que 12 bits (65000 devient 3560, soit ≈ 23 s) et ce délai est à revoir.
3. Étalonnages analogiques AOCI à reprendre : courants (shunt R002, 44 points/A), température (EBiCS lit PB1 comme « moteur » ; sur AOCI PB1 est la sonde contrôleur, PB0 la sonde moteur, tables NTC en `0x0801B1BE` et `0x0801B33C`), tension (59,1 points/V, `CAL_BAT_V` 17 convient), seuils du bouton PA4 (repos 4095 relevé).
4. **PB8** (impulsion au démarrage, précharge probable dans l'origine) non reproduite.
5. Cadence 48 impulsions/tour (EBiCS : 24) et l'émission de la distance (octet 2 de 0x3200 vaut 0 ; l'afficheur AOCI attend 0,01 km sur 2 octets, cumul du trajet).
6. Essai du firmware d'origine avec l'afficheur AOCI pour comparer les trames (CANable).
7. Partie puissance (revue croisée Fangyan, avant essai sous puissance) : `_T` 2000, DTG, ADC injecté, flottants dans l'interruption rapide, coupure surintensité, BKIN, HardFault.
8. ~~Test d'horloge en RAM~~ : fait à l'étape 5. Reste : premier flash (porte 3), après relecture de la partie puissance.

## Étape 5 : essais sur la carte, dossier de revue puissance (16/10/2026)

Carte de banc n° 1 branchée au WCH-Link (alimentée par la sonde seule). **Aucune écriture en flash** ; flash relue en fin de séance, identique au dump de la porte 1. Détail et sorties brutes : oci-firmware-re/validation/porte-3/README.md. Scripts : 	ools/bench/.

**Résultats des essais**
- SystemInit() du portage exécuté en RAM sur la vraie carte : **66 MHz verrouillés, 65,96 MHz mesurés** (132 000 009 cycles DWT en 2,001 s), FLASH_ACR = 0x32 comme l'origine. Repli HSI (quartz simulé absent) : 64 MHz.
- CAN en boucle interne silencieuse (rien sur le bus) : ID AOCI  x12F83200/01 émis et reçus, données exactes, **250,0 kbit/s** mesurés (249,9 en repli HSI).
- TIMER1 4000,0 Hz, TIMER2 500,0 kHz (à 66 et 64 MHz).
- Coût du PI + racine carrée : **flottant 2901 cycles au pire (73 % du budget de 4000), entier 479 (12 %)**.
- Équivalence PI entier / flottant sur PC : écart maximal 2 points sur 4 millions de pas.

**Corrections du code principal faites pendant les essais**
- system_aoci.c : la prélecture flash était annoncée dans le commentaire mais pas activée ; FLASH_ACR = 0x32 relevé sur l'origine (2 états d'attente + prélecture) : ajoutée. Délais surchargeables pour tester le repli.
- config.h : **VOLTAGE_MIN était faux** (1200 : unité 0,025 V supposée à tort) ; l'unité est le point ADC (59,1 points/V) : **1773 pour 30 V**. LEGALFLAG passé à **1** (limite 25 km/h appliquée sauf mode offroad, décision du 16/10). #warning de rappel sur les constantes de puissance M820.
- Makefile : suivi des dépendances d'en-têtes (-MMD -MP) : avant, une modification de config.h ou main.h ne recompilait rien ; cible PROP=1 pour la proposition de revue.

**Dossier de revue puissance** : evue-puissance/ (README, 12 propositions justifiées, modèle de compte rendu, diffs, arbre complet proposed/ qui compile, à ne pas flasher). Constats principaux : protection de surintensité d'EBiCS inopérante (seuil 2800 points > pleine échelle 2048), chien de garde jamais démarré et mal réglé, handlers de faute sans coupure, constantes de temps calculées pour 120 MHz, échelle de courant AOCI environ 30 fois plus grossière que celle supposée par EBiCS, apprentissage des Hall en tension fixe sans limite de courant.

**Toujours interdit de flasher** ni le code principal ni la proposition.