# Propositions pour la partie puissance (brouillon pour revue)

Légende : **[V]** vérifié (lu dans le code ou mesuré sur la carte), **[C]** calculé, **[P]** probable (déduit de l'analyse du firmware AOCI d'origine), **[?]** inconnu, à lever par le développeur.
Les références de code sont données par fonction (les numéros de ligne bougent). `src/` = code courant, `proposed/` = proposition.

## Contexte en dix lignes

- Base : EBiCS, branche M820 (contrôleur de moyeu Bafang, GD32F303, Cortex-M4F, **120 MHz**, FPU). Cible : contrôleur AOCI (APM32F103RC, Cortex-M3 **sans FPU**, **66 MHz**). Brochage PWM / ADC quasi identique [V].
- Le firmware AOCI d'origine est la référence de comportement : décompilé (`aoci-firmware-re/`), registres relevés en direct par SWD (`validation/porte-3/releve-gpio-live.md`).
- L'étage de puissance AOCI : 3 demi-ponts, driver **EG2123A** (verrouillage haut/bas intégré, temps mort intégré 100 à 300 ns, **pas d'entrée d'arrêt**), **pas de coupure matérielle de surintensité** (BKIN désactivé dans l'origine aussi) [V].
- Conséquence : la seule protection de surintensité est logicielle. Si le code a un bug, rien ne rattrape sur la carte : d'où les protections au banc (alimentation de labo limitée en courant, fusible) du plan de portes.
- Le moteur d'origine est piloté en **FOC avec capteur UVW** (codeur magnétique), en **tension sur l'axe q** (pas de PI de courant Iq), avec 3 seuils de surintensité de phase et une machine d'états d'autotest [V, `analyse-commande-moteur.md`].
- EBiCS régule le courant (PI sur iq et id) : comportement différent, à retuner.

## Table des propositions

| # | Sujet | Gravité | Code touché |
|---|---|---|---|
| P1 | Constantes de temps et d'échantillonnage pour 66 MHz | haute | `config.h`, `main.c` (timer0, adc) |
| P2 | Coupure de surintensité (l'existante est inopérante) | **critique** | `safety.c`, `FOC.c`, `main.c` (ISR ADC) |
| P3 | Handlers de faute : couper la puissance avant tout | **critique** | `gd32f30x_it.c` |
| P4 | PI et racine carrée en entiers (budget temps) | haute | `FOC.c`, `foc_int.c`, `main.c` |
| P5 | Chien de garde (aujourd'hui inactif et mal réglé) | haute | `main.c` |
| P6 | Attentes sans fin au démarrage | moyenne | `main.c` |
| P7 | Pas de coupure matérielle : ce qui reste possible | information | (matériel) |
| P8 | Offsets des capteurs de courant mesurés au démarrage | haute | `main.c` |
| P9 | Températures : seuils, voie de mesure, sonde absente | moyenne | `main.c` |
| P10 | État des sorties quand MOE = 0 (OSSR / OSSI) et temps mort | moyenne | `main.c` (timer0) |
| P11 | Échelles de courant et gains du PI (écart d'un facteur ~30) | **critique** | `config.h`, `main.c` |
| P12 | Apprentissage des angles Hall : tension fixe sans limite de courant | **critique** | `main.c` (autodetect), `FOC.c` |

Non traités (voir fin) : autotests de démarrage de l'origine, détection de panne Hall, appels CAN bloquants, polarité des capteurs.

---

## P1. Constantes de temps et d'échantillonnage pour 66 MHz

**Constat [V]**
- `config.h` : `_T = 3750` est la demi-période du compteur centré à 120 MHz (16 kHz). À 66 MHz la même valeur donne **8,8 kHz** au lieu de 16,5 kHz : toutes les constantes de temps de la boucle sont fausses d'un facteur 1,9.
- `main.c` (`timer0_config`) : temps mort `deadtime = 32`. À 120 MHz c'est 267 ns ; à 66 MHz 485 ns. L'origine AOCI utilise **DTG = 25 (379 ns)** [V, BDTR relevé = 0x0C19].
- `main.c` (`adc_config`) : conversions injectées (courants de phase) à **55,5 cycles** ADC. À 11 MHz d'horloge ADC : (55,5 + 12,5) / 11 MHz = **6,2 µs = 409 ticks**. L'origine AOCI échantillonne ces voies à **7,5 cycles** [V, SMPR relevés : ADC1 PA3, ADC2 PA2 et PA5 = 7,5], soit 1,8 µs = 120 ticks.
- Fenêtre disponible : le compteur est centré, le bas est conducteur autour du sommet. Pour `_T = 2000` et `_U_MAX = 1920`, la phase la plus chargée n'a que **55 ticks (0,83 µs)** de bas conducteur au sommet [C, simulation exacte du SVPWM d'EBiCS] ; avec `_T = 3750` il y en a 103 ticks (0,86 µs à 120 MHz) pour une conversion 409 ticks (3,4 µs à 20 MHz) : EBiCS s'appuie donc déjà sur le déclenchement dynamique (`dyn_adc_state`) ; son seuil `DYNAMIC_ADC_THRESHOLD = 3750` est égal à `_T` alors que le CCR maximal atteignable est 3647 (`_U_MAX` = 1920) : **le déclenchement dynamique ne s'active jamais** [C].

Table (fenêtre basse minimale au sommet, tous angles, ticks de 66 MHz, `_T = 2000`) [C] :

| Amplitude u (sur 2048) | CCR max | Fenêtre basse min (ticks / µs) |
|---|---|---|
| 2048 | 2008 (dépasse `_T`, saturation) | -8 |
| 2000 | 1984 | 16 / 0,24 |
| **1920 (`_U_MAX`)** | 1945 | **55 / 0,83** |
| 1700 | 1837 | 163 / 2,5 |
| 1500 | 1738 | 262 / 4,0 |

**Effet** : courants de phase mesurés pendant une commutation (valeur fausse), donc régulation fausse, donc courant non maîtrisé. Fréquence PWM et temps morts hors des valeurs prévues pour le driver.

**Proposition** (`proposed/inc/config.h`, `proposed/src/main.c`)
- `_T = 2000` (66 MHz / 4000 = 16,5 kHz, valeur ARR de l'origine).
- `deadtime = 25`.
- Injectées à `ADC_SAMPLETIME_7POINT5` comme l'origine. Les constantes `ADC_CONV_TICKS = 120` et `ADC_SETTLE_TICKS = 60` (estimation : temps mort 25 + driver jusqu'à 300 ns + rebond) définissent `DYNAMIC_ADC_THRESHOLD = _T - 10 - 120 - 60 = 1810`.

**Pourquoi** : on reprend ce que le firmware AOCI d'origine fait déjà sur cette carte avec le même driver ; les valeurs d'EBiCS ne sont pas transposables (elles supposent 120 MHz et un ADC à 20 MHz).

**Ce qui reste ouvert pour le développeur** : `ADC_SETTLE_TICKS` est une estimation ; `_U_MAX` (1920) laisse 55 ticks au sommet et devra peut-être baisser ; mesurer à l'oscilloscope le signal de shunt et l'instant d'échantillonnage. Vérifier **le nombre de déclenchements ADC par période PWM** (attendu : un seul, soit 16,5 kHz d'interruptions) : l'origine déclenche par TRGO de OC4REF en PWM2 avec CCR4 = 1999 (relevé), EBiCS par l'événement CC4 en PWM0 avec CCR4 = `_T - 10`, le compteur étant en mode centré 3 (l'origine : mode 1). Comparer aussi le mode centré.

**Essais faits** : relevé des registres TIM1 / ADC de l'origine (`porte-3/releve-gpio-live.md`) ; simulation SVPWM (table ci-dessus, script dans le journal d'essai). **À faire** : oscilloscope sur PA8-10 / PB13-15 et sur une voie de courant, puissance coupée puis alim de labo 24 V / 1 A.

---

## P2. Coupure de surintensité : l'existante est inopérante

**Constat [V]** (`FOC.c`, `FOC_calculation`)
```
if(MS_FOC->i_d > (PH_CURRENT_MAX<<2)){   // 700 << 2 = 2800
    ... MOE coupé ...
    while(1){}                            // reste dans l'interruption jusqu'au reset
}
```
- 2800 points dépasse la pleine échelle de l'ADC (± 2048 points) : **cette coupure ne peut jamais se déclencher** [C]. Id est un courant de phase filtré : physiquement il ne dépasse pas la pleine échelle des phases (~2048 points, ~2400 au plus avec les échelles de la transformée de Clarke).
- Elle ne regarde que l'axe d filtré, pas les courants de phase instantanés.
- Elle boucle dans l'interruption : pas de défaut signalé, pas de verrou propre, le chien de garde est de toute façon rechargé ailleurs (P5).
- Le firmware AOCI d'origine [V, `analyse-commande-moteur.md`] : trois seuils de courant de phase (80 / 100 / 120 A nominal, ×1 à ×2 selon la température), actions graduées (Vq -20 par période, puis Vq × 0,9375, puis après **6 périodes consécutives** au troisième seuil : défaut, MOE coupé, état 9), et une logique équivalente hors marche.

**Effet** : un court-circuit de phase ou une dérive de la boucle n'est coupé par rien (ni le driver, qui ne protège que contre le haut et bas simultanés).

**Proposition** (`proposed/src/safety.c`, `proposed/inc/safety.h`)
- `safety_pwm_off()` : écriture directe du bit MOE (aucun appel de bibliothèque, utilisable dans toute interruption).
- `safety_fault(code)` : coupe, puis **verrouille** le défaut (le premier code est conservé). Pas de remise en route sans remise sous tension. Les codes reprennent ceux des clignotements de la LED AOCI (3 = surintensité, 10 = offset...).
- `safety_check_phase_currents(i1, i2, i3)` appelée **à chaque période PWM** dans `ADC0_1_IRQHandler`, sur les trois courants bruts (avant tout calcul) : coupure si `|i| ≥ PH_CURRENT_TRIP` pendant `PH_TRIP_PERIODS = 6` périodes (comme le palier 3 de l'origine), ou **immédiate** si `|i| ≥ 2000` points (saturation de l'amplificateur).
- Le moteur ne repart pas tant qu'un défaut est verrouillé (`main` et `autodetect`).
- Plus de `while(1)` dans une interruption.

**Pourquoi** : on remplace une protection morte par une protection simple, sur la bonne grandeur (courant de phase brut), au bon rythme, avec un comportement de défaut identique à l'origine.

**À relire** : (a) `PH_CURRENT_TRIP = 377` points correspond à 120 A nominal **si** l'échelle est 3,14 points/A [P, voir P11] ; (b) les deux paliers inférieurs de l'origine (réduction progressive de Vq) ne sont pas repris ; (c) le facteur de température de l'origine (×1 à 25 °C jusqu'à ×2 à 150 °C) n'est pas repris : il compense la dérive du Rds(on) **si** la mesure se fait par le Rds(on) des MOSFET bas [P] ; (d) la valeur d'un déclenchement par échantillon isolé (ici debounce de 6 périodes, soit 0,36 ms à 16,5 kHz) est à valider sur la tenue des MOSFET.

**Essais faits** : compilation ; revue du code. **À faire** : injection d'un courant connu (alim de labo + charge) pour vérifier le seuil ; déclenchement forcé et mesure du temps entre le dépassement et la coupure du MOE ; vérifier à l'oscilloscope que les six sorties passent bien à l'état de repos.

---

## P3. Handlers de faute : couper la puissance avant tout

**Constat [V]** (`gd32f30x_it.c`) : `HardFault`, `MemManage`, `BusFault`, `UsageFault` font `while(1){}`; `NMI` est vide. Les timers continuent de tourner : si MOE est actif, le PWM reste émis avec les **derniers rapports cycliques figés**, sans plus aucun contrôle.

**Origine AOCI [V]** : « Arrêt sûr : MOE coupé puis reset ».

**Proposition** : dans les cinq handlers, `safety_pwm_off(); NVIC_SystemReset();`.

**Pourquoi** : le cas le plus problematique (code plante avec la PWM active) devient un arret immediat suivi d'un redemarrage propre. Le reset est indispensable aussi parce que le chien de garde (P5) ne protège pas ce cas tant que les interruptions continuent de tourner.

**À vérifier** : le bootloader AOCI après un reset logiciel reste en attente environ 0,5 s sur le CAN avant de relancer l'appli [V, `bootloader/README.md`] ; après un reset par chien de garde il saute directement dans l'appli (pas de fenêtre de reflashage : il faut couper l'alimentation). **Essai à faire** : provoquer un HardFault puissance active (basse puissance) et mesurer le temps de coupure.

---

## P4. PI et racine carrée en entiers

**Constat [V]** : la boucle rapide (`ADC0_1_IRQHandler`, tous les 60,6 µs = **4000 cycles** à 66 MHz, interruptions masquées pendant tout le traitement) appelle `PI_control` en flottant (deux fois), `sqrtf`, des conversions et un produit en `double` (`runPIcontrol`). Sur Cortex-M3 sans FPU c'est émulé par logiciel.

**Mesure sur la carte [V]** (`tools/bench/t5_ram_cost.py`, 2000 itérations, entrées aléatoires réalistes, limitation de cercle active) :

| Version | min | moyen | **max** | part du budget (pire cas) |
|---|---|---|---|---|
| Flottant (code courant) | 2102 | 2686 | **2901 cycles** | **73 %** |
| Entier (proposition) | 402 | 465 | **479 cycles** | **12 %** |

Ces chiffres ne contiennent que le PI et la racine : Clarke, Park, sinus / cosinus, SVPWM, lecture ADC, `dyn_adc_state` s'y ajoutent dans les 27 % restants. **Le code courant risque de dépasser sa période** (donc de perdre des échantillons de courant et de dériver), ce qui expose directement au point P2.

**Proposition** : PI en Q16 (`FOC.c`, `foc_int.c`), racine carrée entière (`isqrt_u32`, au plus 16 itérations, sans division), `battery_current_max * 0.9` en entier. Gains convertis à la compilation (`Q16()`).

**Vérifié** : équivalence avec le PI flottant d'EBiCS sur PC (`tools/bench/host_test_foc_int.c`) : **écart maximal 2 points, moyen 0,43 point sur 4 millions de pas** ; racine carrée exacte pour tout x < 2^25 et sur 20 millions de tirages 32 bits, identique à `(int)sqrtf((float)x)` pour toutes les valeurs utilisées. Analyse du binaire : plus aucun appel flottant ni 64 bits dans l'arbre d'appels de l'interruption ADC (le code courant en contient : `__aeabi_fdiv`, `__aeabi_fmul`, `__ieee754_sqrtf`, `__aeabi_dmul`...).

**Ouvert** : le calcul reste dans une interruption à interruptions masquées (`__disable_irq()` pendant tout le traitement) ; mesurer le temps total réel de l'interruption (broche de test, oscilloscope) ; les débordements des produits sont maîtrisés dans le PI (delta saturé), pas dans toutes les expressions de `FOC_calculation`.

---

## P5. Chien de garde

**Constat [V]**
- `fwdgt_config(65000, FWDGT_PSC_DIV256)` : le registre de rechargement fait **12 bits** ; 65000 devient 3560 (≈ 23 s). Et **`fwdgt_enable()` n'est jamais appelé** : le chien de garde ne tourne pas [V, vérifié aussi en direct : IWDG_PR = 0, IWDG_RLR = 0xFFF sur la carte avec le firmware d'origine en veille ; l'origine règle /16, 4000 coups = 1,6 s].
- `fwdgt_counter_reload()` est appelé dans `TIMER1_IRQHandler`, `TIMER2_IRQHandler`, `ADC0_1_IRQHandler` : un chien démarré serait rechargé par les interruptions même si la boucle principale est bloquée, donc **inutile** pour détecter un blocage.

**Proposition** : `fwdgt_config(500, FWDGT_PSC_DIV16)` (≈ 200 ms), `fwdgt_enable()` après l'initialisation (≈ 0,6 s de démarrage), rechargement **uniquement** dans la boucle principale et dans les boucles longues (`autodetect`, calibration des offsets). Les rechargements dans les interruptions sont retirés.

**Pourquoi** : l'interruption rapide continue d'appliquer la dernière consigne si la boucle principale se bloque (la consigne est calculée dans la boucle principale, `reg_ADC_processing` / `update_setpoint`). Seul un chien rechargé par la boucle principale la détecte.

**À vérifier** : 200 ms est un compromis ; l'origine prend 1,6 s. Durées à borner : effacement de la page flash d'EEPROM virtuelle (20 à 40 ms, `write_virtual_eeprom`), émissions CAN bloquantes (jusqu'à ~10 ms chacune, voir « non traités »). Après un reset par chien de garde, le bootloader saute directement dans l'appli : prévoir un état sûr et un compteur pour ne pas boucler en redémarrage.

---

## P6. Attentes sans fin au démarrage

**Constat [V]** (`main`) : deux boucles `while(!reg_ADC_flag);` (2000 + 64 passages). Si le déclenchement ADC par TIMER1 ne part pas, la carte reste figée ici indéfiniment, sans chien de garde, sans signalisation.

**Proposition** : `wait_reg_adc_flag()` (≈ 20 ms par attente), défaut verrouillé `FAULT_STARTUP_ADC` (20) en cas d'échec ; sorties restent coupées. (Cette attente doit être revue en même temps que le correctif `volatile` déjà appliqué dans le code courant : sans `volatile`, GCC `-Os` transformait cette boucle en boucle infinie et supprimait tout le programme.)

---

## P7. Pas de coupure matérielle : ce qui reste possible

**Constat [V]** : BKIN est désactivé dans l'origine AOCI comme dans EBiCS ; **PB12 est la broche BKIN du TIM1 mais sert de LED de défaut sur cette carte** [V, relevé GPIO] ; aucun comparateur câblé identifié [?]. Le driver ne protège que du haut et bas simultanés.

**Pistes** (décision du développeur, à valider avec Yanni) : (a) alim de labo en limite de courant + fusible pendant tout le portage (déjà prévu dans la méthode) ; (b) le watchdog analogique de l'ADC (AWD) sur les voies injectées, déclenchant une interruption à seuil dès la conversion (plus rapide que le test logiciel mais reste logiciel) ; (c) modification matérielle : relier un comparateur à PB12 (BKIN) en retirant la LED, pour une vraie coupure matérielle (hors périmètre du portage actuel) ; (d) protéger l'ensemble par la limite de courant du BMS (déjà prévu pour la démo).

---

## P8. Offsets des capteurs de courant mesurés au démarrage

**Constat [V]** : `adc_config` fixe les offsets injectés à **2012 / 2028 / 2020** (« hardcoded, to be improved ») et `CAL_BAT_I_OFFSET = 2035` pour le courant batterie. Lecture ADC des voies **à vide sur la carte AOCI (alimentée par la sonde seule)** [V, `tools/bench/t2_ram_clock_adc.py`] : PA5 = 2045, PA3 = 2050, PA2 = 2045 (phases), **PA0 = 1951** (batterie).
- Écart phases : 33, 22, 25 points, soit environ 7 à 10 A sur l'échelle de mesure de 3,14 points/A supposée [P, P11] (à ne pas prendre au pied de la lettre : l'ampli n'est pas alimenté normalement dans cet essai).
- Écart batterie : 84 points × 37 mA = **3,1 A** de décalage sur la limite de courant batterie.
- L'origine AOCI [V] : offset batterie validé dans **1600..2300**, offsets de phase validés dans **2048 ± 128**, recalibrés à chaque passage par l'autotest et **toutes les ~2 s à l'arrêt**, mesurés avec **seulement le bras bas de la phase mesurée conducteur**.

**Proposition** : `calibrate_current_offsets()` : moyenne de 256 échantillons (conversion régulière par DMA) sur chaque voie, PWM coupée, mêmes plages de validité que l'origine ; hors plage : défaut verrouillé 10. Remplace les constantes (registres JOFR et variable `bat_current_offset`).

**Ouvert** : (a) l'origine mesure l'offset avec le bras bas conducteur, ce qui suggère une mesure **par le Rds(on)** des MOSFET bas [P] : si c'est le cas, l'offset dépend de l'état des MOSFET et la proposition (PWM coupée, sorties au repos) est insuffisante ; (b) recalibration périodique à l'arrêt non reprise ; (c) mesurer les offsets avec la carte complète alimentée.

---

## P9. Températures : seuils, voie de mesure, sonde absente

**Constat [V]**
- EBiCS réduit le courant entre **110 et 130 °C** sur `adc_value[6]` = **PB1**. Sur AOCI, **PB1 est la sonde du contrôleur et PB0 celle du moteur** [V, `brochage.md`] ; l'origine réduit de **85 à 100 °C** (100 % vers 33 %) sur la sonde du contrôleur et n'a **aucune protection sur la sonde moteur**.
- `T_NTC` utilise des constantes de la M820 (pull-up 3500 Ω, NTC 5000 Ω à 23 °C, B = 3398) ; l'origine utilise des tables (`0x0801B1BE`, `0x0801B33C`). Pas de contrôle de plage.
- Lecture sur la carte de banc : **PB0 = PB1 = 4095** (circuit ouvert ou non alimenté). Avec `T_NTC`, 4095 donne **-273 °C** : aucune réduction, aucune alarme.

**Proposition** : réduction de 85 à 100 °C vers 33 % (comme l'origine) ; sonde absente ou hors plage (ADC > 4000 ou < 50) = **pas d'assistance** (équivalent du défaut 15 de l'origine), activable par `TEMP_SENSOR_REQUIRED` (à mettre à 0 pour un essai de banc sans NTC).

**À faire** : extraire les tables NTC de l'origine (`0x0801B1BE` / `0x0801B33C`, 191 entrées u16) et remplacer `T_NTC` ; décider d'une protection sur la sonde moteur (limites du moteur inconnues) ; vérifier à quelle voie correspond chaque table.

---

## P10. État des sorties quand MOE = 0 et temps mort

**Constat [V]** : EBiCS configure `runoffstate` / `ideloffstate` à `DISABLE` : quand MOE = 0, les sorties sont en **haute impédance** ; l'origine AOCI a OSSR = OSSI = 1 (BDTR = 0x0C19) : sorties **forcées au niveau de repos** (haut des demi-ponts bas, bas des demi-ponts « inactif »), niveaux de repos OIS identiques à EBiCS (CR2 = 0x2A71 : haut = 0, bas = 1) [V]. La fiche de l'EG2123A indique que des entrées non connectées bloquent les MOSFET (résistances de rappel internes), donc la haute impédance est tolérable mais indéterminée en cas de bruit.

**Proposition** : `TIMER_ROS_STATE_ENABLE` et `TIMER_IOS_STATE_ENABLE`, comme l'origine ; temps mort 25 (voir P1).

**À vérifier** : au démarrage, avant `timer_primary_output_config`, l'état des sorties à l'oscilloscope (PWM à zéro et pas de chevauchement, critère de la porte 3).

---

## P11. Échelles de courant et gains du PI : écart d'un facteur ~30

**Constat [P]** (déduit des constantes de l'origine ; à confirmer par mesure)
- Origine AOCI : seuil de phase = `I(A) × R × 2 × 32768 / 1000 / 1,956` en unités de 16 par point, R = 1,5 (octet de config flash `0x0803D800` = 2 sur cette carte). Pour 80 A : **251 points de 12 bits**, soit **≈ 3,14 points/A** ; pleine échelle ± 2048 points ≈ ± 650 A.
- EBiCS M820 : le commentaire de `autodetect` donne `i_d_setpoint = 200` « ≈ 2000 mA » : **≈ 100 points/A**. ``PH_CURRENT_MAX = 700` y représenterait ≈ 7 A (sur cette seule hypothèse de 10 mA/point, tirée d'un commentaire du code) et **≈ 220 A sur l'échelle AOCI**.
- Conséquences : (a) les limites de courant de phase d'EBiCS (700) ne limitent plus rien d'utile ; (b) les gains du PI (P = 1,5, I = 0,01 par point d'erreur) donnent une réponse **environ 30 fois plus lente** en grandeur physique (stable mais sans doute très molle) ; (c) la résolution est de 0,32 A par point ; (d) le courant batterie est lui à 27,5 points/A (shunt), cohérent avec `CAL_BAT_I = 37 mA/point`.

**Proposition** (valeurs de départ prudentes, `proposed/inc/config.h`) : `PH_CURRENT_MAX = 60` (≈ 19 A), `BATTERYCURRENT_MAX = 15000` mA (plafond du firmware AOCI 36 V 15 A), `PH_CURRENT_TRIP = 377`, `PH_TRIP_PERIODS = 6`.

**À faire (indispensable avant tout essai de puissance)** : mesurer le nombre de points par ampère de phase (alim de labo + charge résistive en courant continu connu), vérifier la **polarité** (voir questions ouvertes), refaire le tuning du PI.

---

## P12. Apprentissage des angles Hall (commande CAN 0x6200) : tension fixe, sans limite de courant

**Constat [V]** (`autodetect`, `FOC_calculation`) : le moteur est entraîné en boucle ouverte pendant **1080 pas de 1°, 5 ms par pas (5,4 s)** avec **`u_d = 200`** fixe (sur 2048, soit **9,8 % de la tension batterie**, de l'ordre de 3,5 V à 36 V) et `u_q = 0` ; la seule limite est le seuil de surintensité (inopérant avant P2). Cette valeur vient du M820. Le courant résultant = tension / résistance de phase du moteur **AOCI, inconnue [?]** : avec un moteur de résistance de phase de 0,1 à 0,3 Ω, cela fait **12 à 35 A pendant 5,4 s**.

**Effet** : surchauffe des phases et des MOSFET, démagnétisation de rotor improbable mais possible, rotation non maîtrisée si la roue est chargée.

**Mitigation déjà en place** : la commande 0x6200 n'est plus acceptée que de l'outil PC (`CAN_NODE_BESST`), `autodetect` ne démarre pas si un défaut est verrouillé et s'interrompt sur défaut, le chien de garde est rechargé à chaque pas (proposition).

**Proposition à étudier** : mesurer la résistance de phase ; limiter `u_d` à la valeur donnant ≤ 2 à 3 A avec le moteur libre (calcul : u_d ≈ I × R × 2048 / Vbat) ; lancer l'apprentissage uniquement avec l'alim de labo limitée à 1 A, en 24 V, roue libre. Voir si on peut plutôt récupérer les angles par la méthode d'origine (rotation à la main, mesure à la volée) ou à partir de l'offset unique stocké par l'origine (`0x0803B080`).

---

## Non traités (à décider)

1. **Autotests de démarrage de l'origine** [V] : court-circuit de phase (tensions de phase < 3/4 Vbat), phase moteur coupée (une phase à 100 % doit faire monter les trois tensions), amplis de courant proches de mi-échelle, tension d'alimentation du driver (8 à 15 V). EBiCS n'en fait aucun. Les voies existent (PC0-PC2 tensions de phase, PC5 alimentation du driver) mais ne sont pas lues.
2. **Détection de panne Hall** : les états 0 et 7 (aucun capteur alimenté, ou capteur défaillant) tombent hors du `switch` d'EBiCS sans défaut. Sur la carte de banc, **les trois lignes Hall lisent 1** (code 7) sans moteur [V].
3. **Appels CAN bloquants** : chaque émission attend jusqu'à 0xFFFF tours de boucle (≈ 6 à 10 ms) ; sans bus, la boucle principale est ralentie de plusieurs dizaines de ms, ce qui interagit avec le chien de garde (P5).
4. **Polarité des capteurs de courant** : l'origine calcule `I = offset - 2 × JDR` (convention ST inversée) ; EBiCS utilise `i = JDR - offset`. **Un signe inversé transforme la régulation en rétroaction positive.** Doit être vérifié avant tout courant (voir question 2).
5. **Ordre de grandeur de la fréquence de l'interruption rapide** : un déclenchement par période PWM à vérifier (P1).
6. **`uint16_t` / `int16_t` et débordements** dans `FOC_calculation` (`int16_i_q_target`, produits en `q31_t`) à relire avec les nouvelles échelles.

---

## Questions ouvertes pour le développeur

1. **Mesure du courant de phase** : shunts (lesquels, valeur) ou Rds(on) des MOSFET bas ? L'analyse de l'origine penche vers le Rds(on) [P] (offset pris bras bas fermé, seuils compensés en température). Cela change l'instant et les conditions d'échantillonnage, et la calibration.
2. **Polarité** de chaque voie de courant (PA2, PA3, PA5, PA0) : à vérifier par injection d'un courant connu, avant tout essai de puissance.
3. **Points par ampère** (phase et batterie) réels sur cette carte (variante R = 1,5 ou 3,5 : octet flash `0x0803D800` = 2 → 1,5).
4. **Instants d'échantillonnage** : nombre de déclenchements par période, position réelle de l'échantillon par rapport aux commutations, effet de `_U_MAX`.
5. **Résistance, inductance, constante de FCEM du moteur AOCI** (7 paires de pôles [P]) pour le calcul de `u_d` d'apprentissage et le retuning.
6. **Choix** entre : coupure à un seuil unique (proposé) ou reprise des trois paliers de l'origine ; protection supplémentaire sur la sonde moteur ; autotests de démarrage.
7. **Politique de redémarrage** après défaut et après reset par chien de garde (remise sous tension obligatoire, ou nouvel essai automatique limité).
8. **Ordre des phases / sens de rotation** et correspondance des trois bits Hall avec l'ordre PWM (l'apprentissage 0x6200 le détermine, mais l'origine garde un ordre fixe `4 6 2 3 1 5`).

---

## Ordre de relecture proposé

P2, P3 (coupure), puis P11 et P12 (échelles et apprentissage : pré-requis de tout essai), P1 et P10 (temps), P4 (budget), P5 et P6, P8 et P9, enfin P7 et les points non traités.

## Critères pour passer la porte 3 (« firmware sans puissance »)

Rappel de `methode-validation-avant-firmware.md` : PWM à zéro au reset, temps mort mesuré, aucun chevauchement haut / bas, fréquence 16,5 kHz (analyseur logique ou oscilloscope sur PA8-10 / PB13-15) ; coupure testée pour : chien de garde, frein PC13, sous-tension, perte de trame afficheur, capteur de couple débranché, sonde de température ; une capture d'écran par cas.
