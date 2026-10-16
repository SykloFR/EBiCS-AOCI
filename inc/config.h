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
#define WHEEL_CIRCUMFERENCE 2200
#define GEAR_RATIO 80 //11 for BionX IGH3
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
#define REVERSE -1 //1 for normal direction, -1 for reverse //use field Motor Type (Para1[18]) 1 = 1, 0 = -1
#define PUSHASSIST_CURRENT 300
#define VOLTAGE_MIN 1773 //30,0 V : coupure basse tension du firmware AOCI 36V15A d'origine (pack 10S). Unite = points ADC (comparaison avec voltage_raw_filtered), 59,1 points/V sur la carte AOCI : 30 x 59,1 = 1773. Rampe d'assistance sur les 176 points suivants (~3 V).
#define SYSTEM_VOLTAGE 36// in V (cible AOCI 36V 15A, pack 10S)
#define MAX_VOLTAGE 42// in V (10S charge a 4,2 V/element)

//---------------------------------------------------------------------
//torquesensor settings
#define TS_COEF 4
#define TS_MODE
#define TQONAD1
#define TQFILTER 4

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
