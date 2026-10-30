/*
 * config.h
 *
 *  Automatically created by Lishui Parameter Configurator
 *  Author: stancecoke
 */

#ifndef CONFIG_H_
#define CONFIG_H_
#include "stdint.h"

// System constants, don't touch!
#define DISPLAY_TYPE_BAFANG (1<<1)							// For ASCII-Output in Debug mode);
#define DISPLAY_TYPE_DEBUG (1<<0)							// For ASCII-Output in Debug mode);
#define EXTERNAL 1
#define INTERNAL 0
//----------------------------------------------------------------------
// advanced setting, don't touch, if you don't know what you are doing!
/* REVUE-PUISSANCE P1 : constantes de temps recalculees pour le controleur AOCI (SYSCLK 66 MHz) */
#define _T 2000  // demi-periode du compteur centre : 66 MHz / (2 x 2000) = 16,5 kHz (ARR = 2000 releve sur le firmware AOCI d'origine ; 3750 pour 120 MHz sur le GD32F303)
#define TRIGGER_OFFSET_ADC 10
#define TRIGGER_DEFAULT _T-TRIGGER_OFFSET_ADC
#define ADC_CONV_TICKS 120      // (7,5 + 12,5 cycles ADC) x 6 (SYSCLK / horloge ADC) : duree d'une conversion injectee en ticks de 66 MHz
#define ADC_SETTLE_TICKS 60     // temps mort 25 + EG2123A (100 a 300 ns) + rebond : ESTIMATION, a mesurer a l'oscilloscope
#define DYNAMIC_ADC_THRESHOLD (_T-TRIGGER_OFFSET_ADC-ADC_CONV_TICKS-ADC_SETTLE_TICKS) // 1810 : au-dela, le declenchement suit la phase la plus chargee
#define CAL_BAT_V 17
#define CAL_BAT_I 37.0
#define CAL_BAT_I_OFFSET 2035
#define CAL_V 15LL<<8
#define CAL_I 95 //Zurueckgerechnet aus Batteriestrom = Tastverhaeltnis * Motorstrom
#define BOOTLOADER 820
// BionX IGH3
//#define INDUCTANCE 12LL
//#define RESISTANCE 220LL
//#define FLUX_LINKAGE 2400LL
//#define GAMMA 13LL

// Hoverboard Motor
#define INDUCTANCE 11LL
#define RESISTANCE 120LL
#define FLUX_LINKAGE 2000LL
#define GAMMA 13LL


//#define FAST_LOOP_LOG
//#define DISABLE_DYNAMIC_ADC
//#define INDIVIDUAL_MODES
//#define SPEEDTHROTTLE
//#define PRINTDEBUG_UART
//#define PRINTDEBUG_CAN // desactive sur AOCI : trame de debug 0x7AB non prevue sur le bus afficheur
#define R_TEMP_PULLUP 3500
#define SIXSTEPTHRESHOLD 10000
#define SPEED_PLL 0 //1 for using PLL, 0 for angle extrapolation
#define P_FACTOR_PLL 10
#define I_FACTOR_PLL 10

//----------------------------------------------------------------------
//Battery bar settings for Kunteng and Bafang Display
#define BATTERY_LEVEL_1 323000
#define BATTERY_LEVEL_2 329000
#define BATTERY_LEVEL_3 344000
#define BATTERY_LEVEL_4 368000
#define BATTERY_LEVEL_5 380000

//----------------------------------------------------------------------
//PI-control factor settings
#define P_FACTOR_I_Q 1.5
#define I_FACTOR_I_Q 0.01
#define P_FACTOR_I_D 1.5
#define I_FACTOR_I_D 0.01
#define P_FACTOR_SPEED 1
#define I_FACTOR_SPEED 10

//----------------------------------------------------------------------
//PAS mode settings
//#define DIRDET
#define FRAC_HIGH 30
#define FRAC_LOW 15
#define PAS_TIMEOUT 400
#define RAMP_END 1200

//---------------------------------------------------------------------
//Throttle settings
#define THROTTLE_OFFSET 920   //only default value, throttle offset is set at startup automatically
#define THROTTLE_MAX 2850
#define THROTTLE_OVERRIDE

//--------------------------------------------------------------------
//Speed settings
/* Rapport de reduction AOCI Takamori mesure (dents : 8 -> 41 -> 11 -> 81) = (41/8) * (81/11) = 3321/88 ~= 37,74:1
   et rotor a 14 aimants = 7 paires de poles (brochage.md + demontage). */
#define WHEEL_CIRCUMFERENCE 2200
#define GEAR_RATIO 38 //37,7:1 pour AOCI Takamori (80 sur Bafang M820, 11 sur BionX IGH3)
#define SPEEDLIMIT 2500
#define PULSES_PER_REVOLUTION 1 //wheel revolution, Para1[20]
#define SPEEDSOURCE EXTERNAL
#define SPEEDFILTER 1
#define SPDSHFT 0
#define LEGALFLAG 1 //1 = limite de vitesse appliquee (EN 15194, 25 km/h) sauf mode offroad : decision Yanni 16/10/2026

//---------------------------------------------------------------------
//power settings
/* REVUE-PUISSANCE P2 : echelles de courant du controleur AOCI.
   Courant de phase : seuil d'origine = I(A) x R x 2 x 32768 / 1000 / 1,956 unites de 16 par point, R = 1,5 (octet de config flash 0x0803D800 = 2) :
   80 A = 251 points, soit ~3,14 points/A (et non ~100 points/A comme supposent les valeurs M820 d'EBiCS). A CONFIRMER par mesure au banc.
   Courant batterie : 27,5 points/A (shunt R002), CAL_BAT_I = 37 mA/point convient. */
#define PH_COUNTS_PER_A_X100 314
#define PH_CURRENT_MAX 60        // PREMIER ESSAI prudent : ~19 A de phase. EBiCS/M820 : 700 (= ~220 A sur l'echelle AOCI !). A fixer par le developpeur.
#define BATTERYCURRENT_MAX 15000 // mA : plafond du firmware AOCI 36V 15A (EBiCS/M820 : 12000)
#define PH_CURRENT_TRIP 377      // points : 120 A nominal du palier 3 du firmware AOCI d'origine (80 / 100 / 120 A). A relire.
#define PH_TRIP_PERIODS 6        // periodes PWM consecutives au-dessus du seuil avant coupure (origine : 6, soit ~0,36 ms)
#define TEMP_SENSOR_REQUIRED 1   // 1 : sonde de temperature absente / hors plage = pas d'assistance (a mettre a 0 pour un essai de banc sans NTC)
#ifdef BENCH_BUILD
/* Porte 4, 27/10/2026 (t24) : theta decroissant (repere A5) = avant physique ; avec -1 le walk poussait le moteur en
   arriere derriere sa roue libre. +1 : couple vers theta decroissant et decalage 6-step du bon cote (main.c,
   get_standstill_position selon le sens). Hors banc, MP.reverse vient de Para1[18] (afficheur) : a regler a part. */
#define REVERSE 1
#else
#define REVERSE -1 //1 for normal direction, -1 for reverse //use field Motor Type (Para1[18]) 1 = 1, 0 = -1
#endif
#define PUSHASSIST_CURRENT 300
#define VOLTAGE_MIN 1773 //30,0 V : coupure basse tension du firmware AOCI 36V15A d'origine (pack 10S). Unite = points ADC (comparaison avec voltage_raw_filtered), 59,1 points/V sur la carte AOCI : 30 x 59,1 = 1773. Rampe d'assistance sur les 176 points suivants (~3 V).
#define SYSTEM_VOLTAGE 36// in V (cible AOCI 36V 15A, pack 10S)
#define MAX_VOLTAGE 42// in V (10S charge a 4,2 V/element)

/* PORTE 4 (A1, revue Fangyan du 19/10/2026) : build de banc, mingw32-make CORE=m3 BENCH=1 CURRENT_SIGN=1 aoci.
   CURRENT_SIGN = 1 depuis le correctif F6 (26/10/2026, velo) : convention d'EBiCS (runPIcontrol : u_d = -PI(i_d),
   u_q = +PI(i_q)), qui attend u_d -> -i_d et u_q -> +i_q ; avec -1, les deux boucles divergeaient (walk 3 pts).
   Alim de labo 24 V / 0,5-1 A, moteur a vide. Echelle 3,14 points/A (octet 0x0803D800 = 2 lu le 20/10/2026). */
#ifdef BENCH_BUILD
#undef PH_CURRENT_MAX
#define PH_CURRENT_MAX 20        // ~6,4 A de phase (decision de revue du 27/10 : echelle 6 a 11 pts en une seance)
#undef PH_CURRENT_TRIP
#define PH_CURRENT_TRIP 45       // ~14 A de phase : garde-fou court-circuit / amorcage croise (decision du 27/10) ;
                                 // au banc, l'alim limitee a 1,5 A et la duree de 3 s protegent ; production : 377
#undef BATTERYCURRENT_MAX
#define BATTERYCURRENT_MAX 2000  // mA : seconde barriere derriere la limite de l'alim de labo
#undef VOLTAGE_MIN
#define VOLTAGE_MIN 1064         // 18,0 V x 59,1 points/V ; pleine consigne au-dessus de ~21 V (rampe de 176 points)
#undef TEMP_SENSOR_REQUIRED
#define TEMP_SENSOR_REQUIRED 1   // a passer a 0 seulement si la NTC PB1 est invalide en A4 (deviation ecrite)
#define U_MAX_BENCH 1500L        // CCR max ~1738 : 252 ticks au sommet pour 180 necessaires (1640 ne laissait que 3 ticks de marge)
#define BENCH_NO_EEPROM          // page 0x0803F000 ni lue ni ecrite : MP vient des macros ci-dessus (la page contient MP d'un ancien build)
#define BENCH_OL_UD_MAX 60       // outil vecteur fixe : u_d plafonne a 60/2048 de Vbat (~0,7 V a 24 V)
#define BENCH_WALK_ILIM_MAX 15   // home trainer (decision du 27/10) : walk par CAN plafonne a 15 pts ; echelle de seance 6/8/10/11
/* PORTE 4, correctif F1 (26/10/2026, trip a vide sur le velo) : offsets de phase repris pont en commutation, trois phases a
   50 % (vecteur nul), apres la mesure bras bas ferme qui sert de controle. */
#define CAL_PONT_LIM 60          // pts : |brut - offset bras bas ferme| tenu PH_TRIP_PERIODS periodes pendant la mesure -> defaut 10
#define CAL_PONT_ECART_MAX 40    // pts : ecart maximal admis entre les deux methodes, sinon defaut 10
/* PORTE 4, correctif F2 : octet de variante 0x0803D800 (hors image, conserve par le flash). Les seuils en points ci-dessus
   valent pour 2 (R = 1,5, 3,14 pts/A) ; 1 (R = 3,5, 7,33 pts/A) les multiplie par 733 / 314 ; autre valeur : defaut 10. */
#define VARIANTE_ADDR 0x0803D800u
/* PORTE 4, diagnostic du demarrage du pont et precharge F3 (voir main.c, pwm_enable_safe) */
#define CAP_N 128                // periodes PWM capturees apres MOE = 1 (~7,8 ms a 16,5 kHz)
#define CAP_SAT(v) ((int8_t)((v) > 127 ? 127 : ((v) < -127 ? -127 : (v))))
#define PRECHARGE_MAX_MS 5       // precharge des bootstraps reglable par CAN de 0 a 5 ms
#define PT_N 128                 // capture avant trip : periodes PWM gardees (~7,7 ms a 16,6 kHz, voir main.c, E20B)
#define HJ_N 64                  // journal des fronts Hall : nombre de fronts gardes (voir main.c, TIMER2_IRQHandler)
#define PH_COUNTS_PER_A_X100_R35 733
#ifndef CURRENT_SIGN
#error "build banc : CURRENT_SIGN doit etre passe explicitement (CURRENT_SIGN=-1 ou 1), c'est une hypothese a verifier en A5/A6"
#endif
#endif

/* PORTE 4, build ROUTE (decision de revue du 27/10/2026, route-points-bloquants-20261027.md) : premier roulage, terrain
   ferme, rider, niveau 1. Meme code que le banc (sens, premier front, pepins, CCR, table Hall en rotation et ancrage,
   borne d'interpolation, F4, anti-windup, telemetrie en emission seule) ; assistance par l'afficheur ; MP depuis les
   macros (page 0x0803F000 non lue, BENCH_NO_EEPROM) ; REVERSE +1 et CURRENT_SIGN +1 du banc. Seuils de production,
   sauf PH_CURRENT_MAX ramene a 30 pts. _U_MAX reste U_MAX_BENCH (1500) : chaine ADC validee seulement CCR <= 1738. */
#ifdef ROUTE_BUILD
#undef PH_CURRENT_MAX
#define PH_CURRENT_MAX 165       // ~53 A de phase crete (lot Pmax niveau 5, 30/10 : 18 A batterie des 45 tr/min a 33 V,
                                 // calcul-pmax-niveau5-20261030.py ; avant 130, puis 98)
#define SOCLE_PHASE_SUR_BAT_X100 230   // plafond de phase par niveau = 2,3 x courant batterie du niveau (avant 1,75, Z8 48 V)
#define SOCLE_PHASE_SUR_BAT_X100_L5 293 // niveau 5 : 2,93 x 18 A = 165 pts (Pmax des 40-50 tr/min) ; niveaux 1-4 a 2,3 x
#define ROUTE_WALK_PCT 17        // marche assistee : 17 % de 165 = 28 pts, comme 30 % de 98 avant
#undef PH_CURRENT_TRIP
#define PH_CURRENT_TRIP 300      // ~95 A : trip de harnais, 1,8 x le plafond du niveau 5, sous le 1er seuil d'origine (100 A)
                                 // (lot Pmax niveau 5, 30/10 ; avant 245, puis 150),
                                 // A REMONTER A 377 (production, 120 A) AVANT TOUT BUILD CLIENT
#undef BATTERYCURRENT_MAX
#define BATTERYCURRENT_MAX 18000 // mA : controleur 18 A (decision de Yanni, 30/10 ; avant 15000, firmware AOCI 36V 15A)
#undef U_MAX_BENCH
#define U_MAX_BENCH 1920L        // 30/10 soir : 1920 (+6,7 %, CCR max ~1945), mesure validee par host_test_adc_dyn_retard (temps
                                 // d'etablissement mesures, periodes tenues) ; lot B3 : 1800 ; avant 1500
#define TRIGGER_OFFSET_ADC_DYN 65 // avance du declenchement en mode dynamique : l'echantillonnage (45 ticks) finit 20 ticks AVANT
                                 // la commutation de la phase la plus chargee (EBiCS : 10, echantillon 10 ticks apres le front)
#undef VOLTAGE_MIN
#define VOLTAGE_MIN 1773         // production : 30,0 V
#undef SPEEDLIMIT
#define SPEEDLIMIT 1500          // 15 km/h (x100) : plafond tant que la chaine ADC haute modulation n'est pas mesuree
#define ROUTE_NIVEAU_MAX 9       // code assist_level (pas un nombre de niveaux) : 9 = niveau 5 de l'afficheur 5 niveaux
                                 // (codes 2, 4, 6, 8, 9 = niveaux 1 a 5) ; lot B3, 30/10 : niveaux 1 a 5 (avant 6)
#define ROUTE_PROFIL_NIVEAU {31, 43, 57, 67, 165}   // lot Pmax niveau 5 (30/10) : 165 = saturation a ~100 W de jambes a 45 tr/min ; avant 140, 110 ; loi type OSF (29/10, loi-type-osf-attendu-20261029.md) : profil plat
                                 // par niveau 1 a 5, vise P_bat / P_pedale = 1,6 / 2,2 / 2,8 / 3,4 / 5,6 (Z8-OSF HYBRID),
                                 // cale sur eta = 0,83 (confirme par le calage du 29/10) ; niveau 3 : 63 -> 57 (mesure 3,1, lot B3)
#define DYN_T_ECH 45             // 30/10 : echantillonnage ADC en ticks (7,5 cycles x 6), fin = valeur retenue
#define DYN_DTG 25               // 30/10 : temps mort (TIMER0 BDTR = 0x0C19)
#define DYN_STAB_DEFAUT 450      // 30/10 (revue du soir) : conduction du bras bas avant la fin de l'echantillon, en ticks, pour une
#define DYN_STAB_DYN 350         // lecture juste, au sommet (defaut) et en declenchement deplace : mesure sur les anneaux figes des
                                 // 4 essais en pic (E20D) ; ADC_SETTLE_TICKS (60) etait une estimation, 7 a 8 fois trop courte
#define DYN_SEUIL_ROUTE (TRIGGER_DEFAULT + DYN_T_ECH - DYN_DTG - DYN_STAB_DEFAUT)   // 1560 : au-dela, la phase la plus chargee
                                 // n'est plus lisible au sommet et le declenchement la suit (route ; DYNAMIC_ADC_THRESHOLD ailleurs)
#define DYN_CALME 100           // 30/10 soir : aucune commutation dans les 100 ticks avant la fin de l'echantillon (estimation,
                                 // a mesurer a l'oscilloscope), sinon les voies ne sont pas donnees au trip
#define DYN_SUITE_MAX 16         // periodes non mesurables consecutives au plus (modele : 9 a erps 180-400), puis la mesure passe
                                 // telle quelle (garde de saturation, puis trip)
#define ROUTE_COUPLE_REPRISE_MV 30 // 30/10 : reprise de l'assistance si couple > zero + 30 mV (avant 10 : bruit 737-747 mV au repos)
#define ROUTE_TIMER_L5 0        // 30/10 (Yanni) : timer L5 2 min / 8 min retire du firmware moteur, l'afficheur Syklo le gerera
                                 // (homologation : equilibre thermique du moteur) ; 1 = timer du socle (28/10)
#define ROUTE_VARIANTE_REPLI 2   // octet 0x0803D800 vierge (0xFF, lu apres le flash route du 27/10) -> variante 2 mesuree
                                 // sur ce controleur (inc/variante.h) ; signale par le bit 4 de E203
#endif

/* Signe des courants de phase : EBiCS lit i = JDR - offset ; l'origine AOCI calcule I = offset - 2 x JDR.
   Hypothese a verifier au banc (A5 vecteur fixe, A6 premier run) ; hors build banc, comportement EBiCS inchange. */
#ifndef CURRENT_SIGN
#define CURRENT_SIGN 1
#endif

//---------------------------------------------------------------------
//torquesensor settings
#define TS_COEF 4
#define TS_MODE
#define TQONAD1
#ifdef ROUTE_BUILD
#define TQFILTER 5               // IIR 1/32 par impulsion PD2 = 2/3 de tour a 48 impulsions par tour, la constante prevue par
                                 // EBiCS pour ~24 impulsions (29/10, correctif-chaine-couple-attendu-20261029.md) ; avant 4
#else
#define TQFILTER 4
#endif

//---------------------------------------------------------------------
//Display settings
#define DISPLAY_TYPE DISPLAY_TYPE_BAFANG

//---------------------------------------------------------------------
//Regen settings

#define REGEN_CURRENT 800
#define REGEN_CURRENT_MAX 10000
//#define ADC_BRAKE

//---------------------------------------------------------------------
#define AUTODETECT 0

#endif /* CONFIG_H_ */
