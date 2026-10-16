# Revue independante de la partie puissance

Objet : verifier le dossier `revue-puissance/PROPOSITIONS.md` et son code (`proposed/`) contre le
code reel, avant de le transmettre au developpeur / a l'ingenieur puissance.
Methode : lecture du code propose, confrontation aux constantes et registres, verification des
indices `adc_value[]` et de la mise a l'echelle. **Aucun essai sous tension. Rien n'a ete flashe.**

## Verdict global

Le dossier est solide et honnete : il corrige des defauts reels (surintensite inoperante, chien de
garde inactif, handlers de faute sans coupure, PI flottant hors budget a 66 MHz) et separe bien le
verifie de l'estime. **Il n'est pas validable sous tension en l'etat** : les grandeurs physiques de
courant (echelle, polarite, topologie de mesure) sont des hypotheses non mesurees, et tout le
reglage du PI en depend. A traiter comme une base de travail, pas comme une solution.

## Points verifies dans le code propose (lecture seule)

| Point | Verdict | Preuve |
|---|---|---|
| P2 surintensite inoperante | confirme | `PH_CURRENT_MAX<<2 = 2800` > pleine echelle 2048 ; remplacee par `safety_check_phase_currents()` sur les 3 courants bruts, appelee en tete d'ISR avant tout calcul (`main.c` ISR, avant `FOC_calculation`) |
| P4 PI entier | confirme, fidele | `isqrt_u32(d2+q2)` reproduit exactement la mise a l'echelle de `arm_sqrt_q31((d2+q2)<<1)>>16` (verifie numeriquement : 2715 contre 2716) ; `BC_limit_flag` bien `volatile` ; aucun flottant dans l'arbre d'appel ISR |
| P8 offsets au demarrage | logique OK | indices `adc_value[0]=PA0`, `[4]=PA5`, `[7]=PA2`, `[8]=PA3` verifies contre `adc_regular_channel_config` ; JOFR injectes reappliques par voie (ADC0->ph3, ADC1->ph2, ADC2->ph1) : coherent |
| P10 ROS/IOS + temps mort | confirme | `TIMER_ROS_STATE_ENABLE`, `TIMER_IOS_STATE_ENABLE`, `deadtime = 25` |
| volatile | confirme | jeu etendu correct (dont `BC_limit_flag`) |

## Ecarts et residus a corriger (mineurs)

1. **`wait_reg_adc_flag()` (P6)** : la garde vaut `SystemCoreClock/50` = 1 320 000 tours ; a ~6 cycles
   par tour, cela fait **~120 ms**, pas les « ~20 ms » annonces. La garde est donc ~6 fois plus longue
   que documente. Sans danger, mais le budget affiche est faux : a corriger, sinon on compare des
   chiffres non comparables.
2. **`dyn_adc_state()`** : commentaire obsolete « une conversion prend 350 ticks » alors que la
   config utilise `ADC_CONV_TICKS = 120` + `ADC_SETTLE_TICKS = 60` = 180. Deux valeurs contradictoires
   dans le meme fichier : garder une seule source de verite.
3. **`PI_control()`** : `temp5 = p_q16 >> 16;` est un residu de debug (ecriture d'une globale a chaque
   appel d'ISR). A retirer.
4. **Codes de faute morts** : `FAULT_HALL`, `FAULT_VOLTAGE`, `FAULT_TEMP_SENSOR` sont definis mais
   aucun detecteur ne les pose (detection Hall absente, tension non surveillee). Les garder est OK,
   mais ne pas laisser croire qu'ils sont actifs.

## Ce qui bloque un essai sous puissance (a lever avant)

1. **Echelle points/ampere (P11)** : tout repose sur ~3,14 pts/A, deduit de l'octet de config flash
   (R = 1,5). Non mesure. Le seuil `PH_CURRENT_TRIP = 377` (cense valoir 120 A) et la limite
   `PH_CURRENT_MAX = 60` (~19 A) sont des **placeholders** : si la carte est en R = 3,5, 377 ne vaut
   plus 120 A. A mesurer (alim de labo limitee + charge connue).
2. **Polarite des 4 voies de courant** : `i = mesure - offset` cote EBiCS, `offset - 2xJDR` cote
   origine. Un signe inverse transforme la regulation en retroaction positive. A verifier par
   injection avant tout courant.
3. **Topologie de mesure (P8/P11)** : shunt bas ou Rds(on) des MOSFET bas ? L'origine prend l'offset
   bras bas conducteur et compense en temperature, ce qui penche pour le Rds(on). Cela change
   l'instant d'echantillonnage, la calibration d'offset et la compensation thermique. La proposition
   (calibration PWM coupee) serait alors insuffisante.
4. **Declenchement / echantillonnage ADC (P1)** : nombre de declenchements par periode PWM et source de
   trigger (EBiCS utilise CC3, l'origine CC4/TRGO) a valider a l'oscilloscope sur cette puce APM32F103.
   La marge d'echantillonnage a fort `_U_MAX` (55 ticks disponible contre 180 necessaires pres de la
   saturation) suggere que `_U_MAX = 1920` devra etre baisse.
5. **Duree totale de l'ISR** : le bench n'a mesure que le PI + racine (479 cycles entier). Clarke,
   Park, sinus/cosinus, SVPWM, division et `dyn_adc_state` s'ajoutent dans un ISR a interruptions
   masquees : a mesurer (broche de test, oscilloscope) avant de conclure sur la tenue a 16,5 kHz.

## Ordre de travail propose (go / no-go)

- **Porte 3 (sans puissance) : GO.** PWM a zero au reset, temps mort, 16,5 kHz : mesurables sans
  risque. P4, P5, P6, P10, P3, P2 peuvent y etre integres et testes.
- **Avant la moindre mise sous tension : STOP.** Lever d'abord P11 (echelle + polarite) et P8
  (topologie). C'est le prerequis n0 ; sans lui, seuils et PI ne veulent rien dire.
- **P12 (apprentissage Hall) et P9 (temperature) : ne pas activer sous puissance** avant d'avoir
  mesure la resistance de phase du moteur AOCI (sinon 12 a 35 A pendant 5,4 s).

## Conclusion

Rien a refaire dans la structure du dossier. Ce qui manque n'est pas encore du code : c'est de la
**metrologie courant**. Une sous-session dediee (banc, alim de labo limitee en courant, charge
resistive) doit lever l'echelle, la polarite et la topologie avant d'aller plus loin.
