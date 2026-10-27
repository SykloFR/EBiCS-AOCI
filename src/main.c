/*!
    \file    main.c
    \brief   led spark with systick, USART print and key example

   \version 2024-12-20, V3.0.1, firmware for GD32F30x
*/

/*
    Copyright (c) 2024, GigaDevice Semiconductor Inc.

    Redistribution and use in source and binary forms, with or without modification, 
are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice, this 
       list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice, 
       this list of conditions and the following disclaimer in the documentation 
       and/or other materials provided with the distribution.
    3. Neither the name of the copyright holder nor the names of its contributors 
       may be used to endorse or promote products derived from this software without 
       specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT 
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR 
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
OF SUCH DAMAGE.
*/

#include "main.h"
#include "FOC.h"
#include "CAN_Display.h"
#include "parser.h"
#include "aoci_board.h"
#include "safety.h"
#include "bench_can.h"
#include "foc_int.h"
#ifdef BENCH_BUILD
#include "f4_mode_commun.h"
#include "bench_walk_bride.h"
#endif
/* variables partagees avec les interruptions / le DMA : volatile obligatoire en -Os (EBiCS est compile en -O0,
   sans quoi while(!reg_ADC_flag); devient une boucle infinie et tout le code apres est supprime) */
volatile uint16_t adc_value[9];

#define FMC_PAGE_SIZE           ((uint16_t)0x800U)
#define FMC_WRITE_START_ADDR    ((uint32_t)0x0803F000U) //Page 126, Page size 2kB
#define FMC_WRITE_END_ADDR      ((uint32_t)0x0803F800U) //just one page
//#define FMC_OFFSET_PARA0      	((uint32_t)28) //starts after hall angles
//#define FMC_OFFSET_PARA1      	FMC_OFFSET_PARA0 + ((uint32_t)64) //starts after Para1
//#define FMC_OFFSET_PARA2      	FMC_OFFSET_PARA1 + ((uint32_t)64) //starts after Para1
#define FMC_OFFSET_MP			((uint32_t)28) //starts after hall angles
uint32_t *ptrd;
uint32_t address = 0x00000000U;
uint32_t data0   = 0x01234567U;
/* calculate the number of page to be programmed/erased */
uint32_t PageNum = (FMC_WRITE_END_ADDR - FMC_WRITE_START_ADDR) / FMC_PAGE_SIZE;
/* calculate the number of words to be programmed/erased */
uint32_t WordNum = ((FMC_WRITE_END_ADDR - FMC_WRITE_START_ADDR) >> 2);

can_trasnmit_message_struct transmit_message;
can_receive_message_struct receive_message;
volatile FlagStatus receive_flag;
volatile FlagStatus PAS_flag=0;
volatile FlagStatus Speed_flag=0;
volatile FlagStatus reg_ADC_flag=0;
volatile FlagStatus OnOffButton_flag=0;
volatile FlagStatus BC_limit_flag=0;
uint8_t Backwards_counter=0;
uint32_t boot_reset_flags=0;     /* RCU_RSTSCK lu au demarrage (cause du reset), puis efface */
static uint8_t pwm_enable_safe(void);
static void pwm_disable(void);
#ifdef BENCH_BUILD
/* Outil de banc « vecteur fixe » (porte 4, A5) : pilote par SWD, jamais par le CAN.
   bench_ol_enable = 1 et bench_ol_keepalive > 0 (ticks de 4 kHz, a rafraichir) : tension u_d fixe a l'angle bench_ol_angle,
   sans PI, avec le trip de phase actif. u_d plafonne a BENCH_OL_UD_MAX. Arret seul si keepalive tombe a 0. */
volatile uint8_t  bench_ol_enable=0;
volatile int16_t  bench_ol_ud=0;
volatile q31_t    bench_ol_angle=0;
volatile uint16_t bench_ol_keepalive=0;
volatile uint8_t  bench_ol_active=0;
#endif

void nvic_config(void);
void led_config(void);
void gpio_config(void);
void rcu_config(void);
void dma_config(void);
void adc_config(void);
void timer0_config(void); //PWM for Mosfet driver
void timer1_config(void); //PWM for triggering regular ADC
void timer2_config(void); // Hall sensor input (encoder in UVW-mode=
ErrStatus can_networking(void);
void can_networking_init(void);
int32_t speed_PLL (int32_t ist, int32_t soll, uint8_t speedadapt);
void runPIcontrol(void);
void autodetect(void);
int32_t map (int32_t x, int32_t in_min, int32_t in_max, int32_t out_min, int32_t out_max);
void get_standstill_position();
void dyn_adc_state(q31_t angle);
void fmc_program_hall_angles(void);
void fmc_erase_pages(void);
void PAS_processing(void);
void reg_ADC_processing(void);
void UART4_init(void);
int16_t internal_tics_to_speedx100 (uint32_t tics);
int16_t external_tics_to_speedx100 (uint32_t tics);
fmc_state_enum fmc_multi_word_program(uint32_t offset, uint8_t* data, uint8_t words);
void write_virtual_eeprom(void);
void read_virtual_eeprom(void);
uint8_t interpolate_assistfactor(void);
int8_t calculate_SOC(uint16_t voltage, uint8_t cells_in_series);
void print_debug_on_CAN(void);
void Speed_processing(void);
uint16_t map_rezi(int32_t actual_value, int32_t actual_time, int32_t timeout, int32_t decay_base);
uint16_t update_setpoint(void);
int16_t T_NTC(uint16_t ADC);
float u32_to_deg=0.00000008381903171539;
volatile uint16_t slow_loop_counter=0;
volatile uint16_t PAS_counter=0;
uint16_t torque_counter=0;
volatile uint16_t Speed_counter=0;
int32_t ButtonVoltageCumulated=620<<6;
#define iabs(x) (((x) >= 0)?(x):-(x))
#define sign(x) (((x) >= 0)?(1):(-1))
MotorState_t MS;
MotorParams_t MP;
//structs for PI_control
PI_control_t PI_iq;
PI_control_t PI_id;
PI_control_t PI_speed;

uint16_t ui16_timertics=0;
uint8_t ui8_6step_flag=0;
uint8_t ui8_hall_state=0;
uint8_t ui8_hall_state_old=0;
uint8_t ui8_hall_case=0;
uint32_t uint32_tics_filtered=128000;
volatile uint16_t uint16_cadence_filtered=0;
uint8_t ui8_overflow_flag=0;
uint8_t ui8_SPEED_control_flag=0;
volatile uint32_t voltage_raw_cumulated=0;
volatile uint16_t voltage_raw_filtered=0;
int32_t torque_offset_correction=0;
uint32_t ui32_erps_cumulated=0;
int32_t q31_rotorposition_hall=0;
q31_t q31_rotorposition_absolute=0;
int8_t i8_recent_rotor_direction=0;


uint16_t ui16_tim2_recent=0;
volatile uint16_t uint16_full_rotation_counter=0;
volatile uint16_t uint16_half_rotation_counter=0;
uint16_t speedlimitx100_scaled=0;
int16_t phase_current_max_scaled=0;
int8_t assist_level_old=0;
q31_t q31_u_d_temp=0;
q31_t q31_u_q_temp=0;
//Hall64	691967230
//Hall26	-11930205
//Hall32	-811271360
//Hall13	-1479377400
//Hall51	2123622926
//Hall45	1348142805

#if defined(BENCH_BUILD) && __has_include("bench_hall_angles.h")
/* PORTE 4 (A5) : angles du codeur releves sur le moteur monte par tools/bench/t18_releve_angles_codeur.py --entete.
   Sans ce fichier, les angles restent ceux du M820 et le walk assist par CAN est refuse (statut 6). */
#include "bench_hall_angles.h"
int32_t i32_hall_order = BENCH_HALL_ORDER;
#if __has_include("bench_hall_rotation.h")
/* Porte 4, 27/10/2026 (correctif-table-hall-rotation-attendu-20261027.md) : frontieres du sens de marche tirees de la
   rotation (positions relatives) avec le meme ancrage moyen que A5 ; le releve statique A5 les place 2 a 5 deg a cote. */
#include "bench_hall_rotation.h"
int32_t Hall_13 = BENCH_HALL_ROT_13;
int32_t Hall_32 = BENCH_HALL_ROT_32;
int32_t Hall_26 = BENCH_HALL_ROT_26;
int32_t Hall_64 = BENCH_HALL_ROT_64;
int32_t Hall_45 = BENCH_HALL_ROT_45;
int32_t Hall_51 = BENCH_HALL_ROT_51;
#else
int32_t Hall_13 = BENCH_HALL_13;
int32_t Hall_32 = BENCH_HALL_32;
int32_t Hall_26 = BENCH_HALL_26;
int32_t Hall_64 = BENCH_HALL_64;
int32_t Hall_45 = BENCH_HALL_45;
int32_t Hall_51 = BENCH_HALL_51;
#endif
#else
int32_t i32_hall_order =-1;
int32_t Hall_13 = 1825361405;
int32_t Hall_32 = -1789569490;
int32_t Hall_26 = -966367405;
int32_t Hall_64 = -322122295;
int32_t Hall_45 = 381775140;
int32_t Hall_51 = 1169185830;
#endif

const int32_t one_deg = 11930465; //one degree in 2^32 logic

int8_t statehistory[36];
uint8_t historycounter=0;
int32_t i32_full_rotation_flag =-1;

float 	helper=0;
int32_t q31_PLL_error=0;
int32_t q31_rotorposition_PLL=0;
uint8_t ui_8_PLL_counter=0;
uint8_t shutoffcounter=0;
uint16_t offroadcode=0;
volatile uint16_t offroadcounter=0;
uint16_t pulse_counter=0;
volatile uint8_t ui_8_PWM_ON_Flag=0;
int32_t q31_angle_per_tic=0;
//Rotor angle scaled from degree to q31 for arm_math. -180Ã‚Â°-->-2^31, 0Ã‚Â°-->0, +180Ã‚Â°-->+2^31
const int32_t deg_30 = 357913941;
uint16_t switchtime[3];
uint16_t ui16_erps=0;
uint16_t ui16_erps_counter=0;
uint16_t mapped_throttle=0;
uint16_t mapped_torque=0;
char char_dyn_adc_state_old=1;
int16_t i16_ph1_current=0;
int16_t i16_ph2_current=0;
int16_t i16_ph3_current=0;

int8_t i8_reverse_flag = 1;
const q31_t tics_lower_limit = WHEEL_CIRCUMFERENCE*5*3600/(6*GEAR_RATIO*SPEEDLIMIT*10); //tics=wheelcirc*timerfrequency/(no. of hallevents per rev*gear-ratio*speedlimit)*3600/1000000
const q31_t tics_higher_limit = WHEEL_CIRCUMFERENCE*5*3600/(6*GEAR_RATIO*(SPEEDLIMIT+2)*10);
uint8_t i = 0;
uint16_t p = 0;
uint16_t Overrun_strength = 0;
uint16_t Overrun_counter = 0;
uint8_t Overrun_flag = 0;
volatile uint32_t timeout = 0xFFFF;
uint8_t transmit_mailbox = 0;
int32_t battery_current_cumulated=0;
uint32_t Speedx100_cumulated=0;
uint32_t torque_cumulated=0;
uint8_t array_temp[88];

uint8_t level_to_array_element[10]={0,0,1,0,2,0,3,0,4,5}; //map assist Level to array element
uint16_t Poll_commands[3]={0x3201,0x3200,0x3205};
uint8_t pollnumber=0;
int32_t ic1value = 0,AngleFromPWM = 0;
__IO uint16_t dutycycle = 0;
__IO uint16_t frequency = 0;



/*!
    \brief      toggle the led every 500ms
    \param[in]  none
    \param[out] none
    \retval     none
*/
void led_spark(void)
{
    static __IO uint32_t timingdelaylocal = 0U;

    if(timingdelaylocal){

        if(timingdelaylocal < 500U){
            gd_eval_led_on(LED2);
        }else{
            gd_eval_led_off(LED2);
        }

        timingdelaylocal--;
    }else{
        timingdelaylocal = 1000U;
    }
}

/* REVUE-PUISSANCE P6 : attente bornee du drapeau pose par TIMER1 (4 kHz). Une periode dure 250 us ;
   a ~6 cycles par tour de boucle, SystemCoreClock / 300U = 220 000 tours ~= 20 ms sans drapeau = declenchement absent. */
static uint8_t wait_reg_adc_flag(void)
{
    uint32_t guard = SystemCoreClock / 300U;
    while(!reg_ADC_flag && --guard){ }
    return guard ? 1U : 0U;
}

/* REVUE-PUISSANCE P8 : offset du courant batterie, mesure au demarrage (constante M820 CAL_BAT_I_OFFSET = 2035 ;
   releve sur la carte AOCI alimentee par la sonde seule : PA0 = 1951, valeur valide de l'origine : 1600..2300) */
int32_t bat_current_offset = CAL_BAT_I_OFFSET;

/* PORTE 4 (26/10/2026) : vrai pendant la calibration des phases, ou MOE = 1 avec un seul MOSFET bas passant et l'offset
   injecte a 0 : les valeurs lues ne sont pas des courants, l'ISR ne les passe pas au trip. */
volatile uint8_t offset_cal_active = 0;
volatile int16_t offset_cal_ph[3] = {0, 0, 0};   /* derniere mesure bras bas ferme (ph1, ph2, ph3), meme rejetee : lecture SWD */
/* PORTE 4 (26/10/2026, condition de Fangyan pour le home trainer) : la calibration ferme un MOSFET bas par phase ; elle ne se
   fait que roue arretee : aucun changement des entrees Hall PC6-8 depuis 50 ms et vitesse nulle. Sinon elle est reportee
   (boucle lente) et MOE est refuse tant qu'elle n'a pas reussi (offsets_valides, teste dans pwm_enable_safe). */
volatile uint8_t offsets_valides = 0;
volatile uint16_t hall_stable_ticks = 0;          /* ticks de 4 kHz sans changement de PC6-8 (reg_ADC_processing) */
static uint8_t hall_prec = 0xFF;
#define HALL_STABLE_TICKS 200u                    /* 50 ms a 4 kHz */

#ifdef BENCH_BUILD
/* PORTE 4, correctif F1 (26/10/2026) : sur le controleur du velo, le zero bras bas ferme differe de 10 a 15 pts du zero pont en
   commutation (1 a 3 au banc) : trip a vide des u_d 2. Offsets repris trois phases a 50 % (voir offset_cal_pont_50). */
volatile int16_t offset_pont_ph[3] = {0, 0, 0};   /* derniere mesure pont a 50 % (ph1, ph2, ph3), meme rejetee : telemetrie E207 */
volatile uint8_t offset_cal_pont = 0;             /* vrai pendant la mesure pont a 50 % : garde CAL_PONT_LIM dans l'ISR */
/* PORTE 4, correctif F2 : variante lue au demarrage et seuils en points a son echelle */
volatile uint8_t bench_variante = 0;
uint8_t bench_ph_max = PH_CURRENT_MAX, bench_walk_max = BENCH_WALK_ILIM_MAX;
/* PORTE 4, correctif F4 (voir inc/f4_mode_commun.h) : voies lisibles pour la PROCHAINE conversion injectee, tenu par le code
   qui place le declenchement CC4 (dyn_adc_state, offset_cal_pont_50) ; au reset CC4 = TRIGGER_DEFAULT, trois voies lisibles.
   f4_refus : nombre de periodes ou F4 a ete refusee, par raison (indice f4_resultat_t, sature a 0xFFFF), lecture SWD. */
volatile uint8_t f4_masque = F4_MASQUE_TOUTES;
volatile uint16_t f4_refus[4] = {0, 0, 0, 0};
/* PORTE 4, diagnostic du demarrage du pont et correctif candidat F3 (26/10/2026 : defaut 3 des le palier u_d 0 sur le velo,
   zeros pourtant identiques a 1 pt pres) : capture des CAP_N premieres periodes PWM apres chaque MOE = 1 (courants apres
   offset, avant reconstruction, satures a +-127 pts), envoyee par CAN (E208, E209). Precharge F3 : avant le decoupage, les
   trois MOSFET bas fermes pendant bench_precharge_ms ms (0 = sans, reglable par CAN), pour recharger les bootstraps. */
volatile int8_t cap_ph[3][CAP_N];
volatile uint8_t cap_n = CAP_N;                   /* rang de la prochaine periode capturee ; CAP_N : capture finie */
volatile uint8_t cap_trip = 0xFF;                 /* periode ou le defaut verrouille est vu (une periode apres le trip) */
volatile uint8_t cap_seq = 0;                     /* numero de la capture, incremente a chaque MOE = 1 */
volatile uint8_t bench_precharge_ms = 0;
/* PORTE 4, journal des fronts Hall (26/10/2026 : walk 5 pts sans chaine, le moteur vibre sans tourner, erps qui monte) :
   les HJ_N derniers fronts vus par TIMER2_IRQHandler pendant que le pont est actif (cas EBiCS xy, duree depuis le front
   precedent en ticks de 2 us, mode 6-step, sens detecte, angle de secteur retenu). Remis a zero a chaque MOE = 1
   (pwm_enable_safe), fige au defaut verrouille ou a l'arret du pont (pwm_disable). Relu par CAN (E20A). */
volatile uint8_t hj_case[HJ_N], hj_fl[HJ_N], hj_ang[HJ_N];
volatile uint16_t hj_tics[HJ_N];
volatile uint8_t hj_idx = 0, hj_n = 0, hj_gel = 1, hj_seq = 0;
/* PORTE 4, capture avant trip (27/10/2026 : walk 5 pts, defaut 3 a vitesse stable, cause inconnue) : les PT_N dernieres
   periodes PWM vues par le trip pendant que le pont est actif. Par periode : les trois entrees du trip (f4_entrees_trip,
   saturees a +-127 pts) et un etat (b0-1 resultat F4 f4_resultat_t, b2 F4 non evaluee, b3 6-step). Remise a zero a chaque
   MOE = 1 (pwm_enable_safe), figee au defaut verrouille (la periode du trip est la derniere) ou a l'arret du pont
   (pwm_disable). Relue par CAN (E20B). Diagnostic seul : ne change aucune decision. */
volatile int8_t pt_ph[3][PT_N];
volatile uint8_t pt_st[PT_N];
volatile uint8_t pt_idx = 0, pt_n = 0, pt_gel = 1, pt_seq = 0;
/* Capture enrichie (27/10/2026, H3 erreur d'angle / H4 instabilite, capture-enrichie-attendu-20261027.md) : pour la meme
   periode que pt_ph, l'angle applique (q31_rotorposition_absolute >> 24), u_d et u_q / 16 (i8) et un octet (b0-2 etat
   Hall GPIO, b4 limitation en cercle active dans runPIcontrol, b5 FOC calculee dans la periode). Relue par CAN (E20C). */
volatile int8_t pt_ud[PT_N], pt_uq[PT_N];
volatile uint8_t pt_ang[PT_N], pt_x[PT_N];
volatile uint8_t pt_cercle = 0;                   /* runPIcontrol : u_abs > _U_MAX dans le dernier calcul */
static uint8_t pt_k = 0xFF;                       /* rang ecrit par le trip dans la periode en cours (0xFF : aucun) */
/* Pepins d'angle aux fronts Hall (27/10/2026, correctif-pepins-angle-attendu-20261027.md) : front arrive (TIMER2 remis a 0
   par le materiel, CH0IF leve) mais pas encore traite par TIMER2_IRQHandler (meme priorite, IRQ 29 apres l'IRQ 18) :
   l'angle est tenu une periode au lieu d'etre recalcule depuis l'ancienne frontiere (60 deg de retard). */
volatile uint16_t pepins_tenus = 0;               /* nombre de periodes tenues (lecture SWD) */
/* Largeur reelle du secteur (27/10/2026, correctif-largeur-secteur-attendu-20261027.md) : l'interpolation supposait 60 deg
   par secteur (10923 = 60 deg en q31 >> 16) alors que les secteurs releves en A5 font 54,5 a 64 deg. TIMER2_IRQHandler
   pose a chaque front la largeur du secteur de l'etat atteint (frontiere haute - frontiere basse, q31 >> 16). */
volatile uint16_t secteur_q16 = 10923u;
/* Borne de l'interpolation (27/10/2026, correctif-borne-interpolation-attendu-20261027.md) : quand le rotor ralentit
   (moteur charge), la duree depuis le front depasse la duree de reference filtree et l'angle sortait du secteur (26,6 deg
   au-dela, puis defaut 3). Fraction bornee a 1 : l'angle s'arrete a la frontiere de sortie jusqu'au front suivant. */
volatile uint16_t bornes_interp = 0;              /* periodes interpolees bornees (lecture SWD) */
/* Capture avant F4 (27/10/2026, capture-avant-f4-attendu-20261027.md) : pour la meme periode que pt_ph, les trois courants
   apres offset et AVANT f4_mode_commun (i8, satures) ; leur somme separe un decalage de mesure d'un vrai courant
   continu (somme nulle). Relue par CAN (E20D). */
volatile int8_t pt_av[3][PT_N];
static uint8_t pt_tenu = 0;                       /* angle tenu dans la periode en cours (bit 6 de pt_x, E20C) */
#endif

static void suivre_hall(void)
{
    uint8_t h = (uint8_t)((GPIO_ISTAT(GPIOC) >> 6) & 0x07u);
    if (h != hall_prec) { hall_prec = h; hall_stable_ticks = 0; }
    else if (hall_stable_ticks < 0xFFFFu) hall_stable_ticks++;
}

static uint8_t roue_arretee(void) { return hall_stable_ticks >= HALL_STABLE_TICKS && MS.Speedx100 == 0; }

/* CHCTL2 (CCER) en marche : CH0..CH2 hauts actifs hauts, bas actifs bas (bas inverses), CH3 sans sortie, comme l'origine */
#define CHCTL2_RUN 0x0DDDU

/* PORTE 4 (26/10/2026) : offset d'une phase bras bas ferme, comme l'origine AOCI (FUN_0800b814). La carte mesure le courant
   de phase par le Rds(on) des MOSFET bas : sans MOSFET bas passant, le noeud de phase flotte et l'ampli lit sa tension
   (~2920 points mesures le 26/10, MOE = 0), pas un zero de courant.
   MOE = 1 avec CHCTL2 = chctl2 : aucune sortie validee (CHxEN = CHxNEN = 0), donc etat de repos OSSR : OCx = CHxP = 0 (hauts
   bloques), OCxN = CHxNP. CHxNP = 1 bloque le bas (driver actif bas), CHxNP = 0 le rend passant. Un seul interrupteur ferme :
   aucun chemin de courant. 10 ms de stabilisation (origine : 10), puis 64 lectures du registre injecte brut (offset a 0), une
   par periode reguliere (4 kHz). MOE = 0 AVANT de restaurer CHCTL2, quoi qu'il arrive. Moyenne dans *off, 0 si echec. */
static uint8_t offset_cal_phase(uint32_t chctl2, uint32_t adc, int32_t *off)
{
    uint32_t s = 0;
    uint8_t ok = 1;
    adc_inserted_channel_offset_config(adc, ADC_INSERTED_CHANNEL_0, 0);
    TIMER_CHCTL2(TIMER0) = chctl2;
    __disable_irq();
    if (safety_is_latched()) ok = 0;
    else TIMER_CCHP(TIMER0) |= (uint32_t)TIMER_CCHP_POEN;
    __enable_irq();
    for (int i = 0; ok && i < 40; i++) {
        reg_ADC_flag = 0;
        if(!wait_reg_adc_flag()) ok = 0;
        fwdgt_counter_reload();                 /* calibration reportee : appelee aussi chien de garde en marche */
    }
    for (int i = 0; ok && i < 64; i++) {
        reg_ADC_flag = 0;
        if(!wait_reg_adc_flag()) ok = 0;
        s += (uint16_t)adc_inserted_data_read(adc, ADC_INSERTED_CHANNEL_0);
        fwdgt_counter_reload();
    }
    safety_pwm_off();
    TIMER_CHCTL2(TIMER0) = CHCTL2_RUN;
    if (safety_is_latched()) ok = 0;            /* defaut pose pendant la mesure (ISR, garde) : mesure rejetee */
    *off = (int32_t)(s >> 6);
    for (int i = 0; i < 40; i++) {              /* 10 ms MOE = 0 entre deux phases, comme l'origine */
        reg_ADC_flag = 0;
        if(!wait_reg_adc_flag()) break;
        fwdgt_counter_reload();
    }
    return ok;
}

#ifdef BENCH_BUILD
/* PORTE 4, correctif F1 : offsets pris pont en commutation, comme en marche a consigne nulle. MOE = 1, CHCTL2 de marche, les
   trois CCR a _T/2 : vecteur nul, aucune tension nette sur le moteur. switchtime a _T/2 pour que dyn_adc_state laisse le
   declenchement ADC a TRIGGER_DEFAULT (meme instant d'echantillonnage qu'en marche a faible tension). Offsets injectes a 0,
   10 ms de stabilisation, 256 lectures brutes par voie (une par periode a 4 kHz), MOE = 0, 10 ms MOE = 0.
   Garde dans l'ISR pendant la mesure (offset_cal_pont) : |brut - offset bras bas ferme| > CAL_PONT_LIM pendant
   PH_TRIP_PERIODS periodes PWM -> defaut 10. Ici, en secours a 4 kHz, la meme limite sur une lecture -> defaut 10.
   Appelee seulement apres une mesure bras bas ferme valide (offset_cal_ph dans la plage), roue arretee. */
static uint8_t offset_cal_pont_50(int32_t p[3])
{
    static const uint32_t adc[3] = {ADC2, ADC1, ADC0};  /* ph1 PA2, ph2 PA3, ph3 PA5 */
    uint32_t s[3] = {0, 0, 0};
    uint8_t ok = 1;
    for (int k = 0; k < 3; k++) adc_inserted_channel_offset_config(adc[k], ADC_INSERTED_CHANNEL_0, 0);
    switchtime[0] = switchtime[1] = switchtime[2] = _T >> 1;
    timer_channel_output_pulse_value_config(TIMER0, TIMER_CH_0, _T >> 1);
    timer_channel_output_pulse_value_config(TIMER0, TIMER_CH_1, _T >> 1);
    timer_channel_output_pulse_value_config(TIMER0, TIMER_CH_2, _T >> 1);
    timer_channel_output_pulse_value_config(TIMER0, TIMER_CH_3, TRIGGER_DEFAULT);
    f4_masque = F4_MASQUE_TOUTES;                       /* F4 : declenchement par defaut, trois voies lisibles */
    TIMER_CHCTL2(TIMER0) = CHCTL2_RUN;
    offset_cal_pont = 1;
    __disable_irq();
    if (safety_is_latched()) ok = 0;
    else TIMER_CCHP(TIMER0) |= (uint32_t)TIMER_CCHP_POEN;
    __enable_irq();
    for (int i = 0; ok && i < 40; i++) {
        reg_ADC_flag = 0;
        if(!wait_reg_adc_flag()) ok = 0;
        if (safety_is_latched()) ok = 0;
        fwdgt_counter_reload();
    }
    for (int i = 0; ok && i < 256; i++) {
        reg_ADC_flag = 0;
        if(!wait_reg_adc_flag()) ok = 0;
        for (int k = 0; ok && k < 3; k++) {
            int32_t b = (int32_t)(uint16_t)adc_inserted_data_read(adc[k], ADC_INSERTED_CHANNEL_0);
            s[k] += (uint32_t)b;
            if (abs(b - offset_cal_ph[k]) > CAL_PONT_LIM) { safety_fault(FAULT_OFFSET); ok = 0; }
        }
        if (safety_is_latched()) ok = 0;
        fwdgt_counter_reload();
    }
    safety_pwm_off();
    offset_cal_pont = 0;
    if (safety_is_latched()) ok = 0;
    for (int k = 0; k < 3; k++) p[k] = (int32_t)(s[k] >> 8);
    for (int i = 0; i < 40; i++) {
        reg_ADC_flag = 0;
        if(!wait_reg_adc_flag()) break;
        fwdgt_counter_reload();
    }
    return ok;
}

/* PORTE 4, correctif F2 : octet de variante 0x0803D800, hors image (le flash par CAN ne l'efface pas). 2 : R = 1,5, seuils de
   config.h tels quels ; 1 : R = 3,5, seuils x 733 / 314 (arrondi bas). Autre valeur : faux, le demarrage pose le defaut 10. */
static uint8_t variante_init(void)
{
    uint32_t ech;
    bench_variante = *(volatile const uint8_t *)VARIANTE_ADDR;
    if (bench_variante == 2) ech = PH_COUNTS_PER_A_X100;
    else if (bench_variante == 1) ech = PH_COUNTS_PER_A_X100_R35;
    else return 0;
    safety_ph_trip = (int16_t)((PH_CURRENT_TRIP * ech) / PH_COUNTS_PER_A_X100);
    bench_ph_max = (uint8_t)((PH_CURRENT_MAX * ech) / PH_COUNTS_PER_A_X100);
    bench_walk_max = (uint8_t)((BENCH_WALK_ILIM_MAX * ech) / PH_COUNTS_PER_A_X100);
    MP.phase_current_max = bench_ph_max;
    return 1;
}
#endif

/* REVUE-PUISSANCE P8, PORTE 4 : calibration des offsets de courant, moteur a l'arret, au demarrage.
   Batterie (shunt) : moyenne de 256 conversions regulieres, sorties coupees, plage 1600..2300 comme l'origine.
   Phases : bras bas de la phase mesuree ferme (offset_cal_phase), plage 2048 +- 128 comme l'origine (0x7801..0x87FF sur 16
   lectures >> 3). Hors plage -> faux, defaut FAULT_OFFSET (code 10 de l'origine). En cas d'echec les offsets injectes
   repartent a leurs valeurs de depart (adc_config) : la mesure reste fausse, mais MOE ne peut plus etre active (defaut 10).
   Correspondance des voies (origine, CCER 0x880 / 0x808 / 0x088) : CH0 = PA2 (ph1, ADC2), CH1 = PA3 (ph2, ADC1),
   CH2 = PA5 (ph3, ADC0). Le rotor doit etre immobile : un seul MOSFET bas ferme ne fait pas de circuit, mais un rotor qui
   tourne pourrait debiter par la diode d'un autre bras bas. */
static uint8_t calibrate_current_offsets(void)
{
    uint32_t s_bat = 0;
    for (int i = 0; i < 256; i++) {
        s_bat += adc_value[0];   /* PA0 courant batterie */
        reg_ADC_flag = 0;
        if(!wait_reg_adc_flag()) return 0;
        fwdgt_counter_reload();
    }
    int32_t o_bat = (int32_t)(s_bat >> 8), o_ph1 = 0, o_ph2 = 0, o_ph3 = 0;
    if (o_bat < 1600 || o_bat > 2300) return 0;

    offset_cal_active = 1;
    uint8_t ok = offset_cal_phase(0x0880U, ADC2, &o_ph1)   /* bas CH0 passant, PA2 */
              && offset_cal_phase(0x0808U, ADC1, &o_ph2)   /* bas CH1 passant, PA3 */
              && offset_cal_phase(0x0088U, ADC0, &o_ph3);  /* bas CH2 passant, PA5 */
    offset_cal_ph[0] = (int16_t)o_ph1; offset_cal_ph[1] = (int16_t)o_ph2; offset_cal_ph[2] = (int16_t)o_ph3;
    if (!ok || abs(o_ph3 - 2048) > 128 || abs(o_ph2 - 2048) > 128 || abs(o_ph1 - 2048) > 128) {
        adc_inserted_channel_offset_config(ADC0, ADC_INSERTED_CHANNEL_0, 2045);
        adc_inserted_channel_offset_config(ADC1, ADC_INSERTED_CHANNEL_0, 2050);
        adc_inserted_channel_offset_config(ADC2, ADC_INSERTED_CHANNEL_0, 2045);
        offset_cal_active = 0;
        return 0;
    }
#ifdef BENCH_BUILD
    /* correctif F1 : la mesure bras bas ferme sert de controle, les offsets appliques sont ceux du pont a 50 % */
    int32_t p[3] = {0, 0, 0};
    uint8_t okp = offset_cal_pont_50(p);
    offset_pont_ph[0] = (int16_t)p[0]; offset_pont_ph[1] = (int16_t)p[1]; offset_pont_ph[2] = (int16_t)p[2];
    for (int k = 0; okp && k < 3; k++)
        if (abs(p[k] - 2048) > 128 || abs(p[k] - offset_cal_ph[k]) > CAL_PONT_ECART_MAX) okp = 0;
    if (!okp) {
        adc_inserted_channel_offset_config(ADC0, ADC_INSERTED_CHANNEL_0, 2045);
        adc_inserted_channel_offset_config(ADC1, ADC_INSERTED_CHANNEL_0, 2050);
        adc_inserted_channel_offset_config(ADC2, ADC_INSERTED_CHANNEL_0, 2045);
        offset_cal_active = 0;
        return 0;
    }
    o_ph1 = p[0]; o_ph2 = p[1]; o_ph3 = p[2];
#endif
    bat_current_offset = o_bat;
    adc_inserted_channel_offset_config(ADC0, ADC_INSERTED_CHANNEL_0, (uint16_t)o_ph3);
    adc_inserted_channel_offset_config(ADC1, ADC_INSERTED_CHANNEL_0, (uint16_t)o_ph2);
    adc_inserted_channel_offset_config(ADC2, ADC_INSERTED_CHANNEL_0, (uint16_t)o_ph1);
    offset_cal_active = 0;
    return 1;
}

/*!
    \brief      main function
    \param[in]  none
    \param[out] none
    \retval     none
*/

int main(void)
{
#if (BOOTLOADER== 3)
	nvic_vector_table_set(NVIC_VECTTAB_FLASH, 0x4000); //for bootloader v3

#elif (BOOTLOADER== 38)
	nvic_vector_table_set(NVIC_VECTTAB_FLASH, 0xA800); //for bootloader v3.8

#elif (BOOTLOADER== 820)
	nvic_vector_table_set(NVIC_VECTTAB_FLASH, 0x5000); //for bootloader from M820
#endif


    __enable_irq();

	//SCB->VTOR = 0x08004000;
	/* REVUE-PUISSANCE P5 : chien de garde ~200 ms (/16 -> 2,5 kHz, 500 coups). L'ancien fwdgt_config(65000, DIV256) etait tronque
	   a 12 bits (65000 -> 3560, ~23 s) et le chien n'etait de toute facon jamais demarre (pas de fwdgt_enable). Demarre plus bas,
	   apres l'initialisation (les attentes du demarrage durent ~0,6 s). L'origine AOCI : /16, 4000 coups = 1,6 s.
	   NB : dans gd32f10x_fwdgt.c, fwdgt_config() ecrit FWDGT_KEY_ENABLE (0xCCCC) et demarre immediatement l'IWDG !
	   On configure donc uniquement au moment d'activer le chien de garde, apres les boucles de stabilisation ADC. */
#ifdef __FIRMWARE_VERSION_DEFINE
     uint32_t fw_ver = 0;
#endif
    //receive_flag = RESET;
    /* SystemInit() est deja appele par le demarrage (startup_gd32f10x_hd.S) avant main() :
       un second appel (present dans EBiCS) repasserait inutilement l'horloge sur le HSI 8 MHz. */
    /* system clocks configuration */

    /* A1 porte 4 : cause du reset lue avant toute initialisation (le bootloader AOCI lit RCU_RSTSCK sans l'effacer), puis effacee
       pour que le prochain demarrage ne voie que sa propre cause */
    boot_reset_flags = RCU_RSTSCK;
    RCU_RSTSCK |= RCU_RSTSCK_RSTFC;

    rcu_config();
    /* configure systick */
    systick_config();

    /* initialize the LED */
    gd_eval_led_init(LED2);
    gd_eval_hall_init();
    //gd_eval_com_init(EVAL_COM0);
    nvic_config();
    gpio_config();
    /* TIMER configuration */
    timer0_config(); // PWM for Mosfet driver
#ifdef BENCH_BUILD
    /* A1 porte 4 (revue Fangyan, point 7) : apres un reset logiciel (Default_Handler, HardFault, 0x3005, reset SWD) ou chien de
       garde, le verrou en RAM est perdu. Pas de redemarrage automatique : defaut verrouille, remise sous tension obligatoire. */
    if(boot_reset_flags & (RCU_RSTSCK_SWRSTF | RCU_RSTSCK_FWDGTRSTF | RCU_RSTSCK_WWDGTRSTF)){
    	safety_fault(FAULT_CPU_EXCEPTION);
    }
#endif
    timer2_config(); //for hall sensor handling

    /* DMA configuration */
    dma_config();
    /* ADC configuration */
    adc_config();
    /* Démarre TIMER1 (déclencheur 4 kHz des conversions régulières) APRÈS calibration ADC et activation DMA :
       sinon la calibration ADC (et les déclenchements pendant la config) décalent l'anneau DMA de 9 voies. */
    timer1_config(); //trigger regular ADC for testing

    /* initialize CAN and CAN filter */
    can_networking_init();
#ifdef BENCH_BUILD
    bench_can_init();                            /* télémétrie CAN de banc, émission seule (bench_can.c) */
#endif

    /* enable CAN receive FIFO0 not empty interrupt */
    receive_flag = RESET;

    can_interrupt_enable(CAN0, CAN_INTEN_RFNEIE1);
#ifdef PRINTDEBUG_UART
    //start UART4 for debug messages
    UART4_init();
#endif
    /* initialize transmit message */
    transmit_message.tx_sfid = 0x7ab;
    transmit_message.tx_efid = 0x00;
    transmit_message.tx_ft = CAN_FT_DATA;
    transmit_message.tx_ff = CAN_FF_STANDARD;
    transmit_message.tx_dlen = 8;
    //write_virtual_eeprom();


    //initialize MS struct.
	MS.hall_angle_detect_flag=1;
	MS.Speedx100=0; //in km/h*100
	MS.assist_level=2;
	MS.i_q_setpoint = 0;
	MS.i_d_setpoint = 0;
	MS.angle_est=SPEED_PLL;
	MS.pushassist_flag=SET;
	MS.light_flag=SET;
	MS.button_up_flag=SET;
	MS.button_down_flag=SET;
	MS.offroadflag=RESET;
	MS.offroadtics=0;
	MS.pushassist_flag=RESET;
	MS.distance_since_startup=0;

	MP.pulses_per_revolution = PULSES_PER_REVOLUTION;
	MP.wheel_cirumference = WHEEL_CIRCUMFERENCE;
	MP.speedLimitx100=SPEEDLIMIT;
	MP.battery_current_max = BATTERYCURRENT_MAX;
	MP.phase_current_max = PH_CURRENT_MAX;
	MP.TS_coeff = TS_COEF;
	MP.reverse = REVERSE;
	MP.MagicNumber=202;
	MP.Override_Duration=8000;
	MP.decay_base=16;
	MP.angle_correction=0;


	//init PI structs (REVUE-PUISSANCE P4 : gains en Q16, calcules a la compilation)
	PI_id.gain_i=Q16(I_FACTOR_I_D);
	PI_id.gain_p=Q16(P_FACTOR_I_D);
	PI_id.setpoint = 0;
	PI_id.limit_output = _U_MAX;
	PI_id.max_step=15;
	PI_id.shift=11;
	PI_id.limit_i=1800;

	PI_iq.gain_i=Q16(I_FACTOR_I_Q);
	PI_iq.gain_p=Q16(P_FACTOR_I_Q);
	PI_iq.setpoint = 0;
	PI_iq.limit_output = _U_MAX;
	PI_iq.max_step=15;
	PI_iq.shift=11;
	PI_iq.limit_i=_U_MAX;

    //Check, if virtual EEPROM was ever written (or interrupted mid-program). If not, fill it with default values
#ifdef BENCH_NO_EEPROM
    /* A1 porte 4 : la page 0x0803F000 n'est ni lue ni ecrite (dump du 20/10/2026 : MP d'un ancien build, gear_ratio 80).
       InitEEPROM remplit MP avec les macros de config.h ; son ecriture finale est neutralisee dans write_virtual_eeprom(). */
    InitEEPROM(&MP);
#else
    ptrd = (uint32_t *)FMC_WRITE_START_ADDR;
    if(0xFFFFFFFF == (*(ptrd+1)) || MP.MagicNumber != ((const MotorParams_t *)(FMC_WRITE_START_ADDR + FMC_OFFSET_MP))->MagicNumber){
    	InitEEPROM(&MP);
    }
    //read parameters from virtual EEPROM and overwrite the default values
    read_virtual_eeprom();
#endif
    parse_MOparams(&MP);

    /* REVUE-PUISSANCE P6 : attentes du demarrage bornees (avant : while(!reg_ADC_flag) sans fin si le declenchement ADC
       ne part pas, avec la puissance dans un etat indetermine). En cas d'echec : defaut verrouille, sorties coupees. */
    for (int i = 0; i < 2000; i++) {//let the ADC stabilize
    	if(!wait_reg_adc_flag()){ safety_fault(FAULT_STARTUP_ADC); break; }
    	reg_ADC_flag=0;
    }

    for (int i = 0; i < 64; i++) {// get torquesensor offset
    	torque_offset_correction+=adc_value[2];
    	if(!wait_reg_adc_flag()){ safety_fault(FAULT_STARTUP_ADC); break; }
    	reg_ADC_flag=0;

    }
    torque_offset_correction=(torque_offset_correction>>6);
    torque_offset_correction=740-((torque_offset_correction*3300)>>12);
  //  while((adc_value[1])>3000);//safety for bricked throttle

    /* REVUE-PUISSANCE P8 : offsets des capteurs de courant mesures a chaque demarrage (voir calibrate_current_offsets).
       PORTE 4 (26/10/2026) : le defaut 10 est le premier possible. Avant, le trip tournait des la mise en route de l'ADC avec les
       offsets de depart et MOE = 0 : il verrouillait le defaut 3 et la calibration etait sautee (diagnostic trompeur). Le trip
       n'evalue plus les courants que MOE = 1, hors calibration. Pas de calibration avec un defaut deja verrouille (21 apres un
       reset logiciel, 20) : elle exige MOE = 1. */
    /* PORTE 4 (26/10/2026) : roue arretee exigee (Hall stable 50 ms, vitesse nulle), sinon calibration reportee a la boucle
       lente et MOE refuse d'ici la. */
    for (int i = 0; i < (int)HALL_STABLE_TICKS + 1; i++) {
    	reg_ADC_flag=0;
    	if(!wait_reg_adc_flag()){ safety_fault(FAULT_STARTUP_ADC); break; }
    	suivre_hall();
    }
#ifdef BENCH_BUILD
    if(!variante_init()) safety_fault(FAULT_OFFSET);   /* correctif F2 : variante inconnue, pas de calibration ni de MOE */
#endif
    if(!safety_is_latched() && roue_arretee()){
    	if(calibrate_current_offsets()) offsets_valides = 1;
    	else safety_fault(FAULT_OFFSET);
    }

	helper=((float)1.0/((float)1.0+(float)MP.Cadence_exponent));
    //autodetect();

    fwdgt_config(500, FWDGT_PSC_DIV16);
    fwdgt_enable(); /* REVUE-PUISSANCE P5 : le chien de garde demarre ici ; rechargement uniquement dans la boucle principale */

    while (1){
    	fwdgt_counter_reload();
#ifdef BENCH_BUILD
    	bench_can_poll();                        /* une trame de télémétrie au plus par tour, jamais bloquant */
#endif

#if (DISPLAY_TYPE == DISPLAY_TYPE_BAFANG)
    	if(receive_flag){
    		receive_flag = RESET;
    		processCAN_Rx(&MP, &MS);
    	}

#endif
    	if(offroadcounter>4000){
    		offroadcode=0;
    		MS.offroadtics=0;
    	}
    	if(PAS_flag)PAS_processing();
    	if(Speed_flag)Speed_processing();
    	if(reg_ADC_flag)reg_ADC_processing();




    	// switch lights : sorties actives a l'etat BAS sur le controleur AOCI
    	// (PB10 = feu avant, PA15 = feu arriere/stop ; releve live du firmware d'origine : sorties push-pull, ODR = 1 = eteint)
    	// Arriere : allume avec les feux ou au freinage. Le firmware d'origine fait un veilleuse a 500 Hz / 50 % ; ici allume fixe.
    	gpio_bit_write(LIGHT_FRONT_PORT, LIGHT_FRONT_PIN, MS.light_flag ? RESET : SET);
    	gpio_bit_write(LIGHT_REAR_PORT, LIGHT_REAR_PIN, (MS.light_flag || MS.brake_active_flag) ? RESET : SET);

    	//check brake sensor state
    	if(!gpio_input_bit_get(GPIOC,GPIO_PIN_13))MS.brake_active_flag=1;
    	else MS.brake_active_flag=0;
    	// update scaled current and speed
    	if(MS.assist_level!=assist_level_old){
    		speedlimitx100_scaled=MP.speedLimitx100*MP.assist_settings[level_to_array_element[MS.assist_level]][1]/100;
    		phase_current_max_scaled=MP.phase_current_max*MP.assist_settings[level_to_array_element[MS.assist_level]][0]/100;
        	MS.TQfilter=level_to_array_element[MS.assist_level];
        	MS.TQfilter=MP.assist_settings[MS.TQfilter][2];
        	MS.ext_boost_duration=MP.ext_boost_duration[level_to_array_element[MS.assist_level]];
        	MS.ext_boost_strength=MP.ext_boost_strength[level_to_array_element[MS.assist_level]];
        	if(offroadcounter<4000&&offroadcounter>1000){
        		offroadcode+=pow(10,MS.offroadtics)*MS.assist_level;
        		MS.offroadtics++;
        	}

        	if(offroadcode==MP.MagicNumber){
        		MS.offroadflag=!MS.offroadflag;
        		if(MS.offroadflag)MS.offroadtics=9;
        		else MS.offroadtics=8;
        	}
        	offroadcounter=0;

    		assist_level_old=MS.assist_level;

    	}

            if (slow_loop_counter > 200){ //slow loop every 500ms, Timer1 @4kHz interrupt frequency
            	gd_eval_led_toggle(LED2);
#ifdef PRINTDEBUG_UART

            	//printf("%d, %d, %d, %d, %d\r\n",MS.Battery_Current,MS.i_q_setpoint,MP.reverse*MS.i_q,ui16_erps,temp2);
            	printf("%d, %d, %d, %d, %d\r\n",MS.Battery_Current,MS.i_q_setpoint,MP.reverse*MS.i_q,MS.p_human,MS.Speedx100);
#endif

#ifdef PRINTDEBUG_CAN
            	print_debug_on_CAN();
#endif
//            	if((Overrun_strength-mapped_torque)>>3>0){
//            		Overrun_strength-=(Overrun_strength-mapped_torque)>>3;
//            	}
            	p++;

            	if(pollnumber>2)pollnumber=0;
            	sendCAN_Poll(&MP,&MS,Poll_commands[pollnumber]);
            	pollnumber++;
            	MS.int_Temperature = T_NTC(adc_value[6]);
            	MS.SOC = calculate_SOC(MS.Voltage, (uint8_t) ((float)MP.system_voltage/3.6));
            	//toggle speed pin
            	//gpio_bit_write(GPIOB, GPIO_PIN_0,(bit_status)(1-gpio_input_bit_get(GPIOB, GPIO_PIN_0)));
            	if(Speed_counter>20000) MS.Speedx100=0;
            	/* PORTE 4 : calibration reportee (roue en mouvement au demarrage) : faite des que la roue est arretee, pont coupe */
            	if(!offsets_valides && !safety_is_latched() && !ui_8_PWM_ON_Flag && roue_arretee()){
            		fwdgt_counter_reload();
            		if(calibrate_current_offsets()) offsets_valides = 1;
            		else safety_fault(FAULT_OFFSET);
            		fwdgt_counter_reload();
            	}
				slow_loop_counter = 0;

				if(adc_value[5]<2800)shutoffcounter++; //raw value is 4095 without button pressed, about 3300 with "down" button pressed and about 2400 with on/off button pressed.
				else shutoffcounter=0;
				if(shutoffcounter>50){
					timer_primary_output_config(TIMER0,DISABLE); //stop PWM output
					GPIO_BC(GPIOB) = GPIO_PIN_4; //DC/DC enable off
				    GPIO_BC(GPIOB) = GPIO_PIN_5; // Display off
				    //GPIO_BC(GPIOB) = GPIO_PIN_6; // DC/DC off
				}

            }//end slow loop

#ifdef BENCH_BUILD
            /* Outil vecteur fixe (A5) : seulement sans demande d'assistance et sans defaut ; s'arrete seul sans rafraichissement SWD */
            if(bench_ol_enable && bench_ol_keepalive && !MS.i_q_setpoint && !safety_is_latched()){
            	if(!bench_ol_active){
            		bench_ol_active=1;
            		if(!ui_8_PWM_ON_Flag && pwm_enable_safe()){
            			uint16_half_rotation_counter=0;
            			ui_8_PWM_ON_Flag=1;
            		}
            	}
            } else if(bench_ol_active){
            	pwm_disable();
            	bench_ol_enable=0;
            	bench_ol_active=0;
            }
            if(!bench_ol_active){
#endif
            if(MS.i_q_setpoint){
            	/* REVUE-PUISSANCE P2 : jamais de remise en route tant qu'un defaut est verrouille (remise sous tension requise).
            	   Si le moteur est immobile (uint16_half_rotation_counter > 4000), on ne reactive pas MOE tant que la demande
            	   n'est pas repassée par zero ou qu'un front Hall n'a pas remis uint16_half_rotation_counter a 0. */
            	if(!ui_8_PWM_ON_Flag && !safety_is_latched() && uint16_half_rotation_counter <= 4000){
            		get_standstill_position();
            		/* A1 porte 4 (revue Fangyan, point 4) : PI et rapports cycliques remis a zero, puis test du verrou et MOE = 1
            		   sans fenetre (interruptions masquees), voir pwm_enable_safe() */
            		if(pwm_enable_safe()){
						uint16_half_rotation_counter=0;
						ui_8_PWM_ON_Flag=1;
            		}
            	}
            } else {
            	uint16_half_rotation_counter=0;
            }
            if(uint16_half_rotation_counter>4000) {
            	if(ui_8_PWM_ON_Flag){
					pwm_disable(); //Disable PWM if motor is not turning
					i8_recent_rotor_direction=0;
            	}
            }//end half rotation counter
#ifdef BENCH_BUILD
            }
#endif


    }
}

/*!
    \brief      configure the different system clocks
    \param[in]  none
    \param[out] none
    \retval     none
*/
void rcu_config(void)
{
    /* enable GPIOC clock */
    rcu_periph_clock_enable(RCU_GPIOC);
    /* enable GPIOA clock */
    rcu_periph_clock_enable(RCU_GPIOA);
    /* enable DMA clock */
    rcu_periph_clock_enable(RCU_DMA0);
    /* enable TIMER0 clock */
    rcu_periph_clock_enable(RCU_TIMER0);
    /* enable ADC0 clock */
    rcu_periph_clock_enable(RCU_ADC0);
    /* enable ADC1 clock */
    rcu_periph_clock_enable(RCU_ADC1);
    /* enable ADC1 clock */
    rcu_periph_clock_enable(RCU_ADC2);
    /* config ADC clock */
    rcu_adc_clock_config(RCU_CKADC_CKAPB2_DIV6);
}

/*!
    \brief      initialize CAN and filter
    \param[in]  none
    \param[out] none
    \retval     none
*/
void can_networking_init(void)
{
    can_parameter_struct can_parameter;
    can_filter_parameter_struct can_filter;
    /* initialize CAN structures */
    can_struct_para_init(CAN_INIT_STRUCT, &can_parameter);
    can_struct_para_init(CAN_FILTER_STRUCT, &can_filter);
    /* initialize CAN register */
    can_deinit(CAN0);

    /* initialize CAN */
    can_parameter.time_triggered = DISABLE;
    can_parameter.auto_bus_off_recovery = ENABLE;
    can_parameter.auto_wake_up = DISABLE;
    can_parameter.auto_retrans = ENABLE;
    can_parameter.rec_fifo_overwrite = DISABLE;
    can_parameter.trans_fifo_order = DISABLE;
    can_parameter.working_mode = CAN_NORMAL_MODE;
    can_parameter.resync_jump_width = CAN_BT_SJW_1TQ;
    /* 250 kbit/s, comme le firmware AOCI d'origine :
       - nominal : SYSCLK 66 MHz, APB1 33 MHz, prescaler 11 -> 3 MHz, 1+9+2 = 12 tq (echantillonnage 83 %) ;
       - repli HSI : SYSCLK 64 MHz, APB1 32 MHz, prescaler 8 -> 4 MHz, 1+13+2 = 16 tq. */
    if (SystemCoreClock == 66000000U) {
        can_parameter.time_segment_1 = CAN_BT_BS1_9TQ;
        can_parameter.time_segment_2 = CAN_BT_BS2_2TQ;
        can_parameter.prescaler = 11;
    } else {
        can_parameter.time_segment_1 = CAN_BT_BS1_13TQ;
        can_parameter.time_segment_2 = CAN_BT_BS2_2TQ;
        can_parameter.prescaler = 8;
    }
    can_init(CAN0, &can_parameter);

    /* initialize filter */
    /* CAN0 filter number */
    can_filter.filter_number = 0;

    /* initialize filter */
    can_filter.filter_mode = CAN_FILTERMODE_MASK;
    can_filter.filter_bits = CAN_FILTERBITS_32BIT;
    can_filter.filter_list_high = 0x0000;
    can_filter.filter_list_low = 0x0000;
    can_filter.filter_mask_high = 0x0000;
    can_filter.filter_mask_low = 0x0000;
    can_filter.filter_fifo_number = CAN_FIFO1;
    can_filter.filter_enable = ENABLE;
    can_filter_init(&can_filter);


}

/*!
    \brief      configure GPIO
    \param[in]  none
    \param[out] none
    \retval     none
*/
void gpio_config(void)
{
    /* enable can clock */
    rcu_periph_clock_enable(RCU_CAN0);
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_GPIOC);
    rcu_periph_clock_enable(RCU_GPIOD);

    rcu_periph_clock_enable(RCU_AF);
    gpio_pin_remap_config(GPIO_SWJ_SWDPENABLE_REMAP, ENABLE);
    /* configure CAN0 GPIO, CAN0_TX(PA12) and CAN0_RX(PA11) */
    gpio_init(GPIOA, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_12);
    gpio_init(GPIOA, GPIO_MODE_IPU, GPIO_OSPEED_50MHZ, GPIO_PIN_11);
	
    /* config the GPIO as analog mode */
    gpio_init(GPIOA, GPIO_MODE_AIN, GPIO_OSPEED_MAX, GPIO_PIN_0|GPIO_PIN_2|GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7);
    gpio_init(GPIOC, GPIO_MODE_AIN, GPIO_OSPEED_MAX, GPIO_PIN_3|GPIO_PIN_4); //Battery Voltage
    gpio_init(GPIOB, GPIO_MODE_AIN, GPIO_OSPEED_MAX, GPIO_PIN_1); // Motor Temp

    /* PB3 = 12 V, PB4 = DC/DC, PB5 = afficheur, PB12 = LED (comme le firmware d'origine, sorties push-pull).
       PB6 n'est PAS une sortie sur le controleur AOCI (entree flottante dans le firmware d'origine) :
       l'ancien "DC/DC on" de la M820 est retire pour ne pas forcer cette broche. */
    gpio_init(GPIOB, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_12);

    /* Eclairage actif bas : niveau haut (eteint) dans ODR AVANT de passer la broche en sortie, sans impulsion */
    GPIO_BOP(LIGHT_FRONT_PORT) = LIGHT_FRONT_PIN;
    GPIO_BOP(LIGHT_REAR_PORT) = LIGHT_REAR_PIN;
    gpio_init(LIGHT_FRONT_PORT, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, LIGHT_FRONT_PIN);
    gpio_init(LIGHT_REAR_PORT, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, LIGHT_REAR_PIN);

	//delay_1ms(200);
    GPIO_BOP(GPIOB) = GPIO_PIN_4; //set Pin4 (set by Bootloader on BL38)
    //delay_1ms(200);
    GPIO_BOP(GPIOB) = GPIO_PIN_3; //12V on
    //delay_1ms(200);
    GPIO_BOP(GPIOB) = GPIO_PIN_5; // Display on
    //delay_1ms(200);
	//GPIO_BOP(GPIOB) = GPIO_PIN_6; //DC/DC on

    //PD2 Dual PAS2 input pin
    gpio_init(GPIOD, GPIO_MODE_IPU, GPIO_OSPEED_50MHZ, GPIO_PIN_2);
    //PC0 light short circuit detectionß1
    gpio_init(GPIOC, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_50MHZ, GPIO_PIN_0);
	//PC10 PAS1 (white), PC11 PAS2 (pink), PC13 brake
    gpio_init(GPIOC, GPIO_MODE_IPU, GPIO_OSPEED_50MHZ, GPIO_PIN_10|GPIO_PIN_12|GPIO_PIN_13);
    gpio_exti_source_select(GPIO_PORT_SOURCE_GPIOC, GPIO_PIN_SOURCE_12); //Pas1 interrupt
    /* configure key EXTI line */
    exti_init(EXTI_12, EXTI_INTERRUPT, EXTI_TRIG_FALLING);
    exti_interrupt_flag_clear(EXTI_12);

//    gpio_exti_source_select(GPIO_PORT_SOURCE_GPIOC, GPIO_PIN_SOURCE_8); //Encoder z-Pulse interrupt
//    exti_init(EXTI_8, EXTI_INTERRUPT, EXTI_TRIG_RISING);
//    exti_interrupt_flag_clear(EXTI_8);
    //PB2 for external speed sensor
    gpio_init(GPIOB, GPIO_MODE_IPU, GPIO_OSPEED_50MHZ, GPIO_PIN_2);
    gpio_exti_source_select(GPIO_PORT_SOURCE_GPIOB, GPIO_PIN_SOURCE_2);
    /* configure key EXTI line */
    exti_init(EXTI_2, EXTI_INTERRUPT, EXTI_TRIG_FALLING);
    exti_interrupt_flag_clear(EXTI_2);

    /*configure PA8 PA9 PA10(TIMER0 CH0 CH1 CH2) as alternate function*/
    gpio_init(GPIOA,GPIO_MODE_AF_PP,GPIO_OSPEED_50MHZ,GPIO_PIN_8);
    gpio_init(GPIOA,GPIO_MODE_AF_PP,GPIO_OSPEED_50MHZ,GPIO_PIN_9);
    gpio_init(GPIOA,GPIO_MODE_AF_PP,GPIO_OSPEED_50MHZ,GPIO_PIN_10);

    /*configure PB13 PB14 PB15(TIMER0 CH0N CH1N CH2N) as alternate function*/
    gpio_init(GPIOB,GPIO_MODE_AF_PP,GPIO_OSPEED_50MHZ,GPIO_PIN_13);
    gpio_init(GPIOB,GPIO_MODE_AF_PP,GPIO_OSPEED_50MHZ,GPIO_PIN_14);
    gpio_init(GPIOB,GPIO_MODE_AF_PP,GPIO_OSPEED_50MHZ,GPIO_PIN_15);

}
/*!
    \brief      configure the DMA peripheral
    \param[in]  none
    \param[out] none
    \retval     none
*/
void dma_config(void)
{
    /* ADC_DMA_channel configuration */
    dma_parameter_struct dma_data_parameter;

    /* ADC DMA_channel configuration */
    dma_deinit(DMA0, DMA_CH0);

    /* initialize DMA single data mode */
    dma_data_parameter.periph_addr  = (uint32_t)(&ADC_RDATA(ADC0));
    dma_data_parameter.periph_inc   = DMA_PERIPH_INCREASE_DISABLE;
    dma_data_parameter.memory_addr  = (uint32_t)(&adc_value);
    dma_data_parameter.memory_inc   = DMA_MEMORY_INCREASE_ENABLE;
    dma_data_parameter.periph_width = DMA_PERIPHERAL_WIDTH_16BIT;
    dma_data_parameter.memory_width = DMA_MEMORY_WIDTH_16BIT;
    dma_data_parameter.direction    = DMA_PERIPHERAL_TO_MEMORY;
    dma_data_parameter.number       = 9;
    dma_data_parameter.priority     = DMA_PRIORITY_HIGH;
    dma_init(DMA0, DMA_CH0, &dma_data_parameter);

    dma_circulation_enable(DMA0, DMA_CH0);

    /* enable DMA channel */
    dma_channel_enable(DMA0, DMA_CH0);
}
/*!
    \brief      configure the ADC peripheral
    \param[in]  none
    \param[out] none
    \retval     none
*/
void adc_config(void)
{

    /* configure the ADC sync mode
       Sur STM32F103 / APM32F103, ADC_DAUL_INSERTED_PARALLEL_REGULAL_FOLLOWUP_FAST déclenche aussi ADC1 en mode
       régulier décalé de 7 cycles, sans requête DMA séparée, ce qui décale de 4 rangs le tableau DMA adc_value[0..8].
       ADC_DAUL_INSERTED_PARALLEL garde uniquement le parallèle injecté (courants de phase) et laisse le groupe
       régulier d'ADC0 aligné sur adc_value[0..8]. */
    adc_mode_config(ADC_DAUL_INSERTED_PARALLEL);
    /* ADC scan mode function enable */
    adc_special_function_config(ADC0, ADC_SCAN_MODE, ENABLE);
    adc_special_function_config(ADC0, ADC_CONTINUOUS_MODE, DISABLE);
    adc_special_function_config(ADC1, ADC_SCAN_MODE, ENABLE);
    adc_special_function_config(ADC1, ADC_CONTINUOUS_MODE, DISABLE);
    adc_special_function_config(ADC2, ADC_SCAN_MODE, ENABLE);
    adc_special_function_config(ADC2, ADC_CONTINUOUS_MODE, DISABLE);
    /* ADC data alignment config */
    adc_data_alignment_config(ADC0, ADC_DATAALIGN_RIGHT);
    adc_data_alignment_config(ADC1, ADC_DATAALIGN_RIGHT);
    adc_data_alignment_config(ADC2, ADC_DATAALIGN_RIGHT);

    /* ADC channel length config */
    adc_channel_length_config(ADC0, ADC_REGULAR_CHANNEL,9);
    adc_channel_length_config(ADC0, ADC_INSERTED_CHANNEL,1);
    adc_channel_length_config(ADC1, ADC_INSERTED_CHANNEL,1);
    adc_channel_length_config(ADC2, ADC_INSERTED_CHANNEL,1);

    /* ADC regular channel config */
    adc_regular_channel_config(ADC0, 0, ADC_CHANNEL_0, ADC_SAMPLETIME_239POINT5); // PA0 Battery Current
    adc_regular_channel_config(ADC0, 1, ADC_CHANNEL_6, ADC_SAMPLETIME_239POINT5); // PA6 Throttle?
    adc_regular_channel_config(ADC0, 2, ADC_CHANNEL_7, ADC_SAMPLETIME_239POINT5); // PA7 Torque
    adc_regular_channel_config(ADC0, 3, ADC_CHANNEL_13, ADC_SAMPLETIME_239POINT5);// PC3 battery voltage
    adc_regular_channel_config(ADC0, 4, ADC_CHANNEL_5, ADC_SAMPLETIME_239POINT5); // Phase3 current
    adc_regular_channel_config(ADC0, 5, ADC_CHANNEL_4, ADC_SAMPLETIME_239POINT5); // on/off button
    adc_regular_channel_config(ADC0, 6, ADC_CHANNEL_9, ADC_SAMPLETIME_239POINT5);//Motor temperature
    adc_regular_channel_config(ADC0, 7, ADC_CHANNEL_2, ADC_SAMPLETIME_239POINT5);// Phase1 current
    adc_regular_channel_config(ADC0, 8, ADC_CHANNEL_3, ADC_SAMPLETIME_239POINT5);// Phase2 current

    /* REVUE-PUISSANCE P1 : conversions injectees (courants de phase) a 7,5 cycles comme le firmware AOCI d'origine
       (SMPR relevees par SWD : ADC1 PA3 = 7,5 ; ADC2 PA2 et PA5 = 7,5), soit (7,5 + 12,5) / 11 MHz = 1,8 us = 120 ticks.
       EBiCS : 55,5 cycles = 6,2 us = 409 ticks, plus long que la fenetre basse disponible a 16,5 kHz (voir PROPOSITIONS.md P1).
       Les offsets ci-dessous sont des valeurs de depart : calibrate_current_offsets() les remplace au demarrage. */
    adc_inserted_channel_config(ADC0, 0, ADC_CHANNEL_5, ADC_SAMPLETIME_7POINT5);
    adc_inserted_channel_offset_config(ADC0, ADC_INSERTED_CHANNEL_0, 2045);

    adc_inserted_channel_config(ADC1, 0, ADC_CHANNEL_3, ADC_SAMPLETIME_7POINT5);
//    adc_inserted_channel_config(ADC1, 1, ADC_CHANNEL_5, ADC_SAMPLETIME_55POINT5);

    adc_inserted_channel_offset_config(ADC1, ADC_INSERTED_CHANNEL_0, 2050);
//    adc_inserted_channel_offset_config(ADC1, ADC_INSERTED_CHANNEL_1, 2033); //hardcoded, to be improved


    adc_inserted_channel_config(ADC2, 0, ADC_CHANNEL_2, ADC_SAMPLETIME_7POINT5);
    adc_inserted_channel_offset_config(ADC2, ADC_INSERTED_CHANNEL_0, 2045);


    /* ADC trigger config :
       Sur APM32F103RC (comme le firmware AOCI d'origine : TIM1_CR2 = 0x2A71 -> MMS = 7 = OC4REF, TIM1_CCMR2 = 0x7868 -> OC4M = PWM1/PWM2),
       les conversions injectees sont declenchees par TIM1_TRGO (OC4REF) sur ADC0 (maitre en mode parallele injecte avec ADC1) et ADC2. */
    adc_external_trigger_source_config(ADC0, ADC_REGULAR_CHANNEL, ADC0_1_EXTTRIG_REGULAR_T1_CH1);
    adc_external_trigger_source_config(ADC0, ADC_INSERTED_CHANNEL, ADC0_1_EXTTRIG_INSERTED_T0_TRGO);
    adc_external_trigger_source_config(ADC1, ADC_INSERTED_CHANNEL, ADC0_1_2_EXTTRIG_INSERTED_NONE);
    adc_external_trigger_source_config(ADC2, ADC_INSERTED_CHANNEL, ADC2_EXTTRIG_INSERTED_T0_TRGO);
    /* ADC external trigger enable */
    adc_external_trigger_config(ADC0, ADC_REGULAR_CHANNEL, ENABLE);
    adc_external_trigger_config(ADC0, ADC_INSERTED_CHANNEL, ENABLE);
    adc_external_trigger_config(ADC1, ADC_INSERTED_CHANNEL, ENABLE);
    adc_external_trigger_config(ADC2, ADC_INSERTED_CHANNEL, ENABLE);

    /* enable ADC interface */
    adc_enable(ADC0);
    delay_1ms(1);
    /* ADC calibration and reset calibration */
    adc_calibration_enable(ADC0);
    /* enable ADC interface */
    adc_enable(ADC1);
    delay_1ms(1);
    /* ADC calibration and reset calibration */
    adc_calibration_enable(ADC1);
//     /* enable ADC interface */
    adc_enable(ADC2);
    delay_1ms(1);
//    /* ADC calibration and reset calibration */
    adc_calibration_enable(ADC2);
    /* clear the ADC flag */
    adc_interrupt_flag_clear(ADC1, ADC_INT_FLAG_EOC);
    adc_interrupt_flag_clear(ADC1, ADC_INT_FLAG_EOIC);
    /* enable ADC interrupt */
    adc_interrupt_enable(ADC1, ADC_INT_EOIC);

    /* enable DMA request AFTER calibration, and clear any residual regular data */
    (void)ADC_RDATA(ADC0);
    adc_dma_mode_enable(ADC0);
}

/*!
    \brief      configure the timer peripheral
    \param[in]  none
    \param[out] none
    \retval     none
*/
/* PORTE 4, 27/10/2026 (correctif-ccr-prechargement-attendu-20261027.md) : en build de banc, CCR de CH0-CH2 precharges
   (OCxPE = 1, comme l'origine) : les valeurs ecrites par l'ISR ADC, qui finit pres du bas du compteur, au milieu des
   creneaux hauts, ne sont chargees qu'a l'evenement de mise a jour suivant (RCR = 0 : bas et sommet). CH3 (CC4,
   declenchement ADC et masque F4) reste sans prechargement ; RCR, CCPC et COMG inchanges (variable separee). */
#ifdef BENCH_BUILD
#define CCR_PRECHARGE TIMER_OC_SHADOW_ENABLE
#else
#define CCR_PRECHARGE TIMER_OC_SHADOW_DISABLE
#endif
void timer0_config(void)
{
	/* -----------------------------------------------------------------------
	    TIMER0 configuration to:
	    generate 3 complementary PWM signals with 3 different duty cycles:
	    TIMER0CLK is fixed to systemcoreclock, the TIMER0 prescaler is equal to 6000 so the
	    TIMER0 counter clock used is 20KHz.
	    the three duty cycles are computed as the following description:
	    the channel 0 duty cycle is set to 25% so channel 1N is set to 75%.
	    the channel 1 duty cycle is set to 50% so channel 2N is set to 50%.
	    the channel 2 duty cycle is set to 75% so channel 3N is set to 25%.
	  ----------------------------------------------------------------------- */
	    timer_oc_parameter_struct timer_ocintpara;
	    timer_parameter_struct timer_initpara;
	    timer_break_parameter_struct timer_breakpara;
	    rcu_periph_clock_enable(RCU_TIMER0);

	    timer_deinit(TIMER0);

	    /* TIMER0 configuration */
	/* REVUE-PUISSANCE P1/P10 : mode centre 1 (TIMER_COUNTER_CENTER_DOWN, CR1 = 0x31) comme le firmware AOCI d'origine :
	   avec CH3 en PWM1 (CCR3 = 1990), OC3REF monte une seule fois par periode PWM autour du sommet (16,5 kHz).
	   En mode TIMER_COUNTER_CENTER_BOTH (CR1 = 0x61), le drapeau de comparaison est pose a la montee ET a la descente. */
	    timer_initpara.prescaler         = 0;
	    timer_initpara.alignedmode       = TIMER_COUNTER_CENTER_DOWN;
	    timer_initpara.counterdirection  = TIMER_COUNTER_UP;
	    timer_initpara.period            = _T; //for 32 kHz center aligned --> 16kHz PWM frequency
	    timer_initpara.clockdivision     = TIMER_CKDIV_DIV1;
	    timer_initpara.repetitioncounter = 0;
	    timer_init(TIMER0,&timer_initpara);

	     /* CH1,CH2 and CH3 configuration in PWM mode */
	    timer_ocintpara.outputstate  = TIMER_CCX_ENABLE;
	    timer_ocintpara.outputnstate = TIMER_CCXN_ENABLE;
	    timer_ocintpara.ocpolarity   = TIMER_OC_POLARITY_HIGH;//inverted logic to make ouput in timer logic
	    timer_ocintpara.ocnpolarity  = TIMER_OCN_POLARITY_LOW;
	    timer_ocintpara.ocidlestate  = TIMER_OC_IDLE_STATE_LOW;
	    timer_ocintpara.ocnidlestate = TIMER_OCN_IDLE_STATE_HIGH;

	    timer_channel_output_config(TIMER0,TIMER_CH_0,&timer_ocintpara);
	    timer_channel_output_config(TIMER0,TIMER_CH_1,&timer_ocintpara);
	    timer_channel_output_config(TIMER0,TIMER_CH_2,&timer_ocintpara);
	    /* CH3 (CC4) ne pilote que TRGO (OC3REF), pas de sortie externe (CCER = 0x0DDD comme l'origine) */
	    timer_ocintpara.outputstate  = TIMER_CCX_DISABLE;
	    timer_channel_output_config(TIMER0,TIMER_CH_3,&timer_ocintpara);

	    timer_channel_output_pulse_value_config(TIMER0,TIMER_CH_0,_T>>1);//grün
	    timer_channel_output_mode_config(TIMER0,TIMER_CH_0,TIMER_OC_MODE_PWM0);
	    timer_channel_output_shadow_config(TIMER0,TIMER_CH_0,CCR_PRECHARGE);

	    timer_channel_output_pulse_value_config(TIMER0,TIMER_CH_1,_T>>1);//gelb
	    timer_channel_output_mode_config(TIMER0,TIMER_CH_1,TIMER_OC_MODE_PWM0);
	    timer_channel_output_shadow_config(TIMER0,TIMER_CH_1,CCR_PRECHARGE);

	    timer_channel_output_pulse_value_config(TIMER0,TIMER_CH_2,_T>>1);//blau
	    timer_channel_output_mode_config(TIMER0,TIMER_CH_2,TIMER_OC_MODE_PWM0);
	    timer_channel_output_shadow_config(TIMER0,TIMER_CH_2,CCR_PRECHARGE);

	    timer_channel_output_pulse_value_config(TIMER0,TIMER_CH_3,(TRIGGER_DEFAULT));//(_T>>1)+500 in the middle of the PWM cycle
	    /* Comme le firmware AOCI d'origine (CCMR2 = 0x7868, CR2 = 0x2A71) :
	       CH3 (CC4) en PWM1 (= PWM mode 2 ST, actif quand CNT >= CCR4, donc front montant unique autour du sommet CNT = _T)
	       et TRGO = OC3REF (MMS = 0x70) pour declencher ADC0/ADC1/ADC2 injectes une seule fois par periode PWM (16,5 kHz). */
	    timer_channel_output_mode_config(TIMER0,TIMER_CH_3,TIMER_OC_MODE_PWM1);
	    timer_channel_output_shadow_config(TIMER0,TIMER_CH_3,TIMER_OC_SHADOW_DISABLE);
	    timer_master_output_trigger_source_select(TIMER0,TIMER_TRI_OUT_SRC_O3CPRE);
	    timer_automatic_output_disable(TIMER0);
	    /* automatic output enable, break, dead time and lock configuration*/
	    /* REVUE-PUISSANCE P1/P10 : temps mort 25 (379 ns a 66 MHz) comme l'origine AOCI (BDTR relevee : DTG = 25) ; avant : 32 (calcule
	       pour 120 MHz : 267 ns). Le driver EG2123A ajoute 100 a 300 ns. OSSR/OSSI actives comme l'origine (BDTR = 0xC19) :
	       sorties forcees au niveau de repos (haut = bas, bas = haut) quand MOE = 0, et non en haute impedance. */
	    timer_breakpara.runoffstate      = TIMER_ROS_STATE_ENABLE;
	    timer_breakpara.ideloffstate     = TIMER_IOS_STATE_ENABLE ;
	    timer_breakpara.deadtime         = 25;
	    timer_breakpara.breakpolarity    = TIMER_BREAK_POLARITY_LOW;
	    timer_breakpara.outputautostate  = TIMER_OUTAUTO_DISABLE;
	    timer_breakpara.protectmode      = TIMER_CCHP_PROT_0;
	    timer_breakpara.breakstate       = TIMER_BREAK_DISABLE;
	    timer_break_config(TIMER0,&timer_breakpara);
		timer_channel_output_pulse_value_config(TIMER0,TIMER_CH_0,(_T>>1)-0);
		timer_channel_output_pulse_value_config(TIMER0,TIMER_CH_1,(_T>>1)+0);
		timer_channel_output_pulse_value_config(TIMER0,TIMER_CH_2,(_T>>1)+0);
	    timer_primary_output_config(TIMER0,DISABLE);

	    /* auto-reload preload disable */
	    timer_auto_reload_shadow_disable(TIMER0);
	    timer_enable(TIMER0);

}

void timer1_config(void) //running at 6kHz interrupt frequency
{
    timer_oc_parameter_struct timer_ocintpara;
    timer_parameter_struct timer_initpara;

    rcu_periph_clock_enable(RCU_TIMER1);

    /* TIMER1 configuration : base de temps lente 4 kHz (declenche les conversions regulieres).
       Horloge timers APB1 = 2 x APB1 = SYSCLK (66 MHz nominal, 64 MHz en repli) :
       prescaler 1 -> 33 MHz, periode = 33 MHz / 4 kHz - 1 = 8249 (7999 en repli). */
    timer_initpara.prescaler         = 1;
    timer_initpara.alignedmode       = TIMER_COUNTER_EDGE;
    timer_initpara.counterdirection  = TIMER_COUNTER_UP;
    timer_initpara.period            = (SystemCoreClock / 2U) / 4000U - 1U;
    timer_initpara.clockdivision     = TIMER_CKDIV_DIV1;
    timer_initpara.repetitioncounter = 0;
    timer_init(TIMER1, &timer_initpara);

    /* CH0 configuration in PWM mode0 */
    timer_channel_output_struct_para_init(&timer_ocintpara);
    timer_ocintpara.ocpolarity  = TIMER_OC_POLARITY_HIGH;
    timer_ocintpara.outputstate = TIMER_CCX_ENABLE;
    timer_channel_output_config(TIMER1, TIMER_CH_1, &timer_ocintpara);

    timer_channel_output_pulse_value_config(TIMER1, TIMER_CH_1, 2000);
    timer_channel_output_mode_config(TIMER1, TIMER_CH_1, TIMER_OC_MODE_PWM0);
    timer_channel_output_shadow_config(TIMER1, TIMER_CH_1, TIMER_OC_SHADOW_DISABLE);



    /* TIMER0 primary output enable : uniquement pour les timers avances (TIMER0/TIMER7).
       ATTENTION : en GD32F10x, TIMER_CCHP(TIMER1) vaut 0x40000044 (bit-band SRAM !), qui ecrase l'adresse 0x20000000 bit 17 ! */
    //timer_primary_output_config(TIMER1, ENABLE);
    /* auto-reload preload enable */
    timer_auto_reload_shadow_enable(TIMER1);
    timer_interrupt_flag_clear(TIMER1,TIMER_INT_FLAG_UP);
        /* channel 0 interrupt enable */
    timer_interrupt_enable(TIMER1,TIMER_INT_FLAG_UP);

    /* enable TIMER0 */
    timer_enable(TIMER1);
}


void timer2_config(void) //for hall sensor processing.
{

    /* TIMER2 configuration: input capture mode */
    timer_ic_parameter_struct timer_icinitpara;
    timer_parameter_struct timer_initpara;

    rcu_periph_clock_enable(RCU_TIMER2);


    timer_deinit(TIMER2);
    /* hall mode config */
    timer_hall_mode_config(TIMER2, TIMER_HALLINTERFACE_ENABLE);



    /* TIMER2 configuration : base de temps Hall 500 kHz (le calcul de vitesse suppose 500 kHz),
       horloge timers = SYSCLK : 66 MHz / 132 = 500 kHz (prescaler 131) ; repli 64 MHz : prescaler 127 */
    timer_initpara.prescaler         = (uint16_t)(SystemCoreClock / 500000U - 1U);
    timer_initpara.alignedmode       = TIMER_COUNTER_EDGE;
    timer_initpara.counterdirection  = TIMER_COUNTER_UP;
    timer_initpara.period            = 0xFFFF;
    timer_initpara.clockdivision     = TIMER_CKDIV_DIV1;
    timer_initpara.repetitioncounter = 0;
    timer_init(TIMER2,&timer_initpara);

    /* TIMER2  configuration */
    /* TIMER2 input capture configuration */
    timer_icinitpara.icpolarity  = TIMER_IC_POLARITY_RISING;
    timer_icinitpara.icselection = TIMER_IC_SELECTION_ITS;
    timer_icinitpara.icprescaler = TIMER_IC_PSC_DIV1;
    timer_icinitpara.icfilter    = 0xFF;
    timer_input_capture_config(TIMER2,TIMER_CH_0,&timer_icinitpara);

    timer_icinitpara.icpolarity  = TIMER_IC_POLARITY_RISING;
    timer_icinitpara.icselection = TIMER_IC_SELECTION_ITS;
    timer_icinitpara.icprescaler = TIMER_IC_PSC_DIV1;
    timer_icinitpara.icfilter    = 0xFF;
    timer_input_capture_config(TIMER2,TIMER_CH_1,&timer_icinitpara);

    timer_icinitpara.icpolarity  = TIMER_IC_POLARITY_RISING;
    timer_icinitpara.icselection = TIMER_IC_SELECTION_ITS;
    timer_icinitpara.icprescaler = TIMER_IC_PSC_DIV1;
    timer_icinitpara.icfilter    = 0xFF;
    timer_input_capture_config(TIMER2,TIMER_CH_2,&timer_icinitpara);

    /* slave mode selection: TIMER2 */
    timer_input_trigger_source_select(TIMER2,TIMER_SMCFG_TRGSEL_CI0F_ED);
    timer_slave_mode_select(TIMER2,TIMER_SLAVE_MODE_RESTART);

    /* hall mode config */
    timer_hall_mode_config(TIMER2, TIMER_HALLINTERFACE_ENABLE);

    /* auto-reload preload enable */
    timer_auto_reload_shadow_enable(TIMER2);
    /* clear channel 0 interrupt bit */
    timer_interrupt_flag_clear(TIMER2,TIMER_INT_FLAG_CH0);
    /* channel 0 interrupt enable */
    timer_interrupt_enable(TIMER2,TIMER_INT_CH0);

    /* TIMER2 counter enable */
    timer_enable(TIMER2);

}



/*!
    \brief      configure the nested vectored interrupt controller
    \param[in]  none
    \param[out] none
    \retval     none
*/
void nvic_config(void)
{
	/* configure CAN0 NVIC */
    nvic_irq_enable(CAN0_RX1_IRQn,0,0);
    nvic_irq_enable(EXTI10_15_IRQn, 2U, 0U); //for PAS
    nvic_irq_enable(EXTI2_IRQn, 2U, 0U); //for external speed sensor


    //timer2 interrupt for Halls
    nvic_priority_group_set(NVIC_PRIGROUP_PRE1_SUB3);
    nvic_irq_enable(TIMER1_IRQn, 0, 0);
    nvic_irq_enable(TIMER2_IRQn, 0, 0);
    nvic_irq_enable(ADC0_1_IRQn, 0, 0);
    /* A1 porte 4 : TIMER4_IRQn n'est plus activee (aucun handler : elle tombait dans Default_Handler ; TIMER4 n'est pas utilise) */

}

void UART4_init(void)
{
    /* enable GPIO clock */
    rcu_periph_clock_enable(RCU_GPIOC); //for UART4 Tx on PC12
    //rcu_periph_clock_enable(RCU_GPIOD); //for UART4 Rx on PD2

    /* enable USART clock */
    rcu_periph_clock_enable(RCU_UART4);

    /* connect port to USARTx_Tx */
    gpio_init(GPIOC, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_12);

    /* connect port to USARTx_Rx */
   // gpio_init(GPIOD, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_50MHZ, GPIO_PIN_2);

    /* USART configure */
    usart_deinit(UART4);
    usart_baudrate_set(UART4, 115200U);
    //usart_receive_config(UART4, USART_RECEIVE_ENABLE);
    usart_transmit_config(UART4, USART_TRANSMIT_ENABLE);
    usart_enable(UART4);

    printf("\n\ra Bafang Debug on UART4!\n\r");

}



void TIMER1_IRQHandler(void) // regular ADC processing and common slow timing tasks
{
	/* REVUE-PUISSANCE P5 : pas de fwdgt_counter_reload() ici */
	if(SET == timer_interrupt_flag_get(TIMER1,TIMER_INT_FLAG_UP)){
        /* clear channel 0 interrupt bit */
        timer_interrupt_flag_clear(TIMER1,TIMER_INT_FLAG_UP);

        reg_ADC_flag=1;
//        pulse_counter++;
//        if(pulse_counter>1000)gpio_bit_set(GPIOB,GPIO_PIN_8);
//        else gpio_bit_reset(GPIOB,GPIO_PIN_8);
//        if(pulse_counter>1020)pulse_counter=0;
    }
}


void TIMER2_IRQHandler(void)
{
	/* REVUE-PUISSANCE P5 : pas de fwdgt_counter_reload() ici */
    if(SET == timer_interrupt_flag_get(TIMER2,TIMER_INT_FLAG_CH0)){
        /* clear channel 0 interrupt bit */
        timer_interrupt_flag_clear(TIMER2,TIMER_INT_FLAG_CH0);

       // if(TIM2->CCR1>20)ui16_timertics = TIM2->CCR1; //debounce hall signals
            /* read channel 0 capture value */

        	ui16_timertics=timer_channel_capture_value_register_read(TIMER2,TIMER_CH_0);
        	ui32_erps_cumulated-=ui32_erps_cumulated>>5;
        	ui32_erps_cumulated+=500000/(ui16_timertics*6);
        	ui16_erps=ui32_erps_cumulated>>5;
                  	//Hall sensor event processing

            		ui8_hall_state = (GPIO_ISTAT(GPIOC)>>6)&0x07; //Mask input register with Hall 1 - 3 bits

            		ui8_hall_case=ui8_hall_state_old*10+ui8_hall_state;
            		statehistory[historycounter]=ui8_hall_case;
            		historycounter++;
            		if (historycounter>35)historycounter=0;

            		if(MS.hall_angle_detect_flag){ //only process, if autodetect procedere is fininshed
            		ui8_hall_state_old=ui8_hall_state;
            		}

            			uint32_tics_filtered-=uint32_tics_filtered>>3;
            			uint32_tics_filtered+=ui16_timertics;

            		   ui8_overflow_flag=0;
            		   ui8_SPEED_control_flag=1;



            		switch (ui8_hall_case) //12 cases for each transition from one stage to the next. 6x forward, 6x reverse
            				{
            			//6 cases for forward direction
            		//6 cases for forward direction
            		case 64:
            			q31_rotorposition_hall = Hall_64;

            			i8_recent_rotor_direction = -i32_hall_order;
            			uint16_full_rotation_counter = 0;
            			break;
            		case 45:
            			q31_rotorposition_hall = Hall_45;

            			i8_recent_rotor_direction = -i32_hall_order;
            			break;
            		case 51:
            			q31_rotorposition_hall = Hall_51;

            			i8_recent_rotor_direction = -i32_hall_order;
            			break;
            		case 13:
            			q31_rotorposition_hall = Hall_13;

            			i8_recent_rotor_direction = -i32_hall_order;
            			uint16_half_rotation_counter = 0;
            			break;
            		case 32:
            			q31_rotorposition_hall = Hall_32;

            			i8_recent_rotor_direction = -i32_hall_order;
            			break;
            		case 26:
            			q31_rotorposition_hall = Hall_26;

            			i8_recent_rotor_direction = -i32_hall_order;
            			break;

            			//6 cases for reverse direction
            		case 46:
            			q31_rotorposition_hall = Hall_64;

            			i8_recent_rotor_direction = i32_hall_order;
            			break;
            		case 62:
            			q31_rotorposition_hall = Hall_26;

            			i8_recent_rotor_direction = i32_hall_order;
            			break;
            		case 23:
            			q31_rotorposition_hall = Hall_32;

            			i8_recent_rotor_direction = i32_hall_order;
            			uint16_half_rotation_counter = 0;
            			break;
            		case 31:
            			q31_rotorposition_hall = Hall_13;

            			i8_recent_rotor_direction = i32_hall_order;
            			break;
            		case 15:
            			q31_rotorposition_hall = Hall_51;

            			i8_recent_rotor_direction = i32_hall_order;
            			break;
            		case 54:
            			q31_rotorposition_hall = Hall_45;

            			i8_recent_rotor_direction = i32_hall_order;
            			uint16_full_rotation_counter = 0;
            			break;

            		} // end case
#ifdef BENCH_BUILD
            		switch (ui8_hall_state) {           /* largeur du secteur atteint (meme valeur dans les deux sens) */
            		case 4: secteur_q16 = (uint16_t)(((uint32_t)Hall_45 - (uint32_t)Hall_64) >> 16); break;
            		case 5: secteur_q16 = (uint16_t)(((uint32_t)Hall_51 - (uint32_t)Hall_45) >> 16); break;
            		case 1: secteur_q16 = (uint16_t)(((uint32_t)Hall_13 - (uint32_t)Hall_51) >> 16); break;
            		case 3: secteur_q16 = (uint16_t)(((uint32_t)Hall_32 - (uint32_t)Hall_13) >> 16); break;
            		case 2: secteur_q16 = (uint16_t)(((uint32_t)Hall_26 - (uint32_t)Hall_32) >> 16); break;
            		case 6: secteur_q16 = (uint16_t)(((uint32_t)Hall_64 - (uint32_t)Hall_26) >> 16); break;
            		default: break;                    /* etat 0 / 7 : largeur precedente */
            		}
            		/* journal des fronts Hall (diagnostic du demarrage) : fige des qu'un defaut est verrouille */
            		if(safety_is_latched()) hj_gel = 1;
            		if(!hj_gel){
            			hj_case[hj_idx] = ui8_hall_case;
            			hj_tics[hj_idx] = ui16_timertics;
            			hj_fl[hj_idx] = (uint8_t)((ui8_6step_flag ? 1u : 0u) | (i8_recent_rotor_direction > 0 ? 2u : 0u)
            			                | (i8_recent_rotor_direction < 0 ? 4u : 0u) | (ui_8_PWM_ON_Flag ? 8u : 0u));
            			hj_ang[hj_idx] = (uint8_t)((uint32_t)q31_rotorposition_hall >> 24);
            			hj_idx = (uint8_t)((hj_idx + 1u) % HJ_N);
            			if(hj_n < HJ_N) hj_n++;
            		}
#endif

            		if(MS.angle_est){
            			q31_PLL_error=q31_rotorposition_PLL-q31_rotorposition_hall;
            			if(iabs(q31_PLL_error) < deg_30){
            				if(ui_8_PLL_counter<12)ui_8_PLL_counter++;
            			}
            			else ui_8_PLL_counter=0;
            			q31_angle_per_tic = speed_PLL(q31_rotorposition_PLL,q31_rotorposition_hall,0);
            		}

            	#ifdef SPEED_PLL
            		if(ui16_erps>30){   //360 interpolation at higher erps
            			if(ui8_hall_case==32||ui8_hall_case==23){
            				q31_angle_per_tic = speed_PLL(q31_rotorposition_PLL,q31_rotorposition_hall, SPDSHFT*tics_higher_limit/(uint32_tics_filtered>>3));

            			}
            		}
            		else{

            			q31_angle_per_tic = speed_PLL(q31_rotorposition_PLL,q31_rotorposition_hall, SPDSHFT*tics_higher_limit/(uint32_tics_filtered>>3));
            		}

            	#endif


    }

}



void EXTI10_15_IRQHandler(void)
{
    if(RESET != exti_interrupt_flag_get(EXTI_12)) {
    	PAS_flag = 1;
        exti_interrupt_flag_clear(EXTI_12);
    }
}

void EXTI2_IRQHandler(void)
{
    if(RESET != exti_interrupt_flag_get(EXTI_2)) {
    	Speed_flag = 1;
        exti_interrupt_flag_clear(EXTI_2);
    }
}

void PAS_processing(void)
{
	if(PAS_counter>70){
		MS.cadence=10000/PAS_counter;//24 Pulses per crank revolution, 4000 Hz Timer interrupt frequency (for M560 about 48 pulses on speed/direction pin)(4000*60/24)=10000
		uint16_cadence_filtered-=uint16_cadence_filtered>>3;
		uint16_cadence_filtered+=MS.cadence;


		PAS_flag = 0;
		if(gpio_input_bit_get(GPIOD,GPIO_PIN_2)){
			if(Backwards_counter<10)Backwards_counter++;

		}
		else{
			if(Backwards_counter)Backwards_counter--;
			PAS_counter=0;
		}
		torque_cumulated-=torque_cumulated>>MS.TQfilter;
		if(MS.torque_on_crank>750){
			torque_cumulated+=(MS.torque_on_crank-750);
		}
		//Power=2*Pi*speed*torque, calibration factors: rpm to 1/s for cadence: /60, mV to Nm: 750 to 3200 --> 0 to 80 Nm. (from Bafang data sheet)
		MS.torque_filtered=(torque_cumulated>>MS.TQfilter);
		MS.p_human=(uint16_t)((float)(MS.cadence*MS.torque_filtered)*0.00342); //in Watt

		//PAS_counter=0;
	}
}

void Speed_processing(void)
{
		Speedx100_cumulated-=Speedx100_cumulated/MP.pulses_per_revolution;
		Speedx100_cumulated+=MP.wheel_cirumference*4*360/(MP.pulses_per_revolution*Speed_counter);// 4000 Hz Timer interrupt frequency
		MS.Speedx100=Speedx100_cumulated/MP.pulses_per_revolution;
		Speed_counter=0;
		Speed_flag=0;
		MS.distance_since_startup+=MP.wheel_cirumference/(MP.pulses_per_revolution*1000); //in m
}

void reg_ADC_processing(void)
{
#ifdef BENCH_BUILD
	if(bench_ol_keepalive) bench_ol_keepalive--;   /* 4 kHz : 8000 = 2 s sans rafraichissement SWD avant arret de l'outil */
	bench_can_sample(i16_ph1_current, i16_ph2_current, i16_ph3_current);   /* courants efficaces de la trame E100 */
#endif
	suivre_hall();                                  /* garde de calibration : Hall stable 50 ms */
	battery_current_cumulated-=battery_current_cumulated>>6;
	battery_current_cumulated+= (adc_value[0]-bat_current_offset);   /* REVUE-PUISSANCE P8 : offset mesure au demarrage */
	MS.Battery_Current=(int32_t)((float)(battery_current_cumulated>>6)*CAL_BAT_I); //Battery current in mA
	voltage_raw_cumulated-=voltage_raw_cumulated>>6;
	voltage_raw_cumulated+=adc_value[3];
	voltage_raw_filtered=voltage_raw_cumulated>>6;

	temp1=MS.Battery_Current;
	temp2=MS.u_q;
	temp3=MS.i_q;
	temp4=MS.i_q_setpoint;

	MS.Voltage=voltage_raw_filtered*CAL_BAT_V;//Battery voltage in mV
	MS.calories=MP.angle_correction/one_deg;
	MS.torque_on_crank=(((adc_value[2])*3300)>>12)+torque_offset_correction; //map ADC value to mV
	if(MS.torque_on_crank>760&&PAS_counter<MP.PAS_timeout)torque_counter=0;//reset counter, if pressure on pedal and pedals rotating
	MS.range=Overrun_flag*100;//on/off button line
    slow_loop_counter ++;
    if(torque_counter<64000)torque_counter++;
    if(PAS_counter<64000)PAS_counter++;
    if(Speed_counter<64000)Speed_counter++;
    if(uint16_half_rotation_counter<64000)uint16_half_rotation_counter++;
    if(offroadcounter<64000)offroadcounter++;
    if(ui16_erps_counter<64000)ui16_erps_counter++;
    if(Overrun_counter<64000)Overrun_counter++;
    MS.i_q_setpoint = update_setpoint();
    if (torque_counter>4000&&!Overrun_flag){ //reset after one second without torque on the pedal
    	if (PAS_counter>MP.PAS_timeout){
			Backwards_counter=0;
			MS.cadence=0;
			MS.p_human=0;
			uint16_cadence_filtered=0;
    	}
		torque_cumulated=0;
		if (!MS.i_q_setpoint){//reset integral part, if no power from throttle signal is wanted
			PI_iq.integral_part=0;
			PI_id.integral_part=0;
		}
    }

	reg_ADC_flag=0;
}

int16_t internal_tics_to_speedx100 (uint32_t tics){
	return WHEEL_CIRCUMFERENCE*50*3600/(6*GEAR_RATIO*tics);
}

int16_t external_tics_to_speedx100 (uint32_t tics){
	return MP.wheel_cirumference*4*360/(MP.pulses_per_revolution*tics);
}

int32_t speed_PLL (int32_t ist, int32_t soll, uint8_t speedadapt)
  {
    int32_t q31_p;
    static int32_t q31_d_i = 0;
    static int32_t q31_d_dc = 0;
    //temp6 = soll-ist;
  //  temp5 = speedadapt;
    q31_p=(soll - ist)>>(P_FACTOR_PLL-speedadapt);   				//7 for Shengyi middrive, 10 for BionX IGH3
    q31_d_i+=(soll - ist)>>(I_FACTOR_PLL-speedadapt);				//11 for Shengyi middrive, 10 for BionX IGH3

    //clamp i part to twice the theoretical value from hall interrupts
    if (q31_d_i>((deg_30>>18)*500/ui16_timertics)<<16) q31_d_i = ((deg_30>>18)*500/ui16_timertics)<<16;
    if (q31_d_i<-((deg_30>>18)*500/ui16_timertics)<<16) q31_d_i =- ((deg_30>>18)*500/ui16_timertics)<<16;


    if (!ist&&!soll)q31_d_i=0;

    q31_d_dc=q31_p+q31_d_i;
    return (q31_d_dc);
  }

void runPIcontrol(void){

	//check, if Battery Current limit is exceeded
	if(MS.Battery_Current>MP.battery_current_max) BC_limit_flag=1;
	//check, if theoretical Battery current would be below limit with some hysteresis
	if((MS.i_q_setpoint*CAL_I*MS.u_abs)>>11<((int32_t)MP.battery_current_max*9/10)) BC_limit_flag=0; //duty cycle is scaled to 2048 = 2^11 ; REVUE-PUISSANCE P4 : *0.9 en entier (avant : produit en double emule dans l'interruption)

	if(!BC_limit_flag){
	//control iq
	  PI_iq.recent_value = MS.i_q;
	  PI_iq.setpoint = MP.reverse*i8_reverse_flag*MS.i_q_setpoint;

	}
	else{
	 //control Battery_Current
	  PI_iq.recent_value = MP.reverse*i8_reverse_flag*MS.Battery_Current>>6;
	  PI_iq.setpoint = MP.reverse*i8_reverse_flag*(MP.battery_current_max>>6);

	}
	q31_u_q_temp =  PI_control(&PI_iq);
	//control id
	  PI_id.recent_value = MS.i_d;
	  PI_id.setpoint = MS.i_d_setpoint;
	  q31_u_d_temp = -PI_control(&PI_id); //control direct current to zero

	  //circle limitation

	  /* REVUE-PUISSANCE P4 : racine carree entiere (avant : sqrtf flottant emule, ~1 000 cycles dans l'interruption rapide) */
	  MS.u_abs = (int32_t)isqrt_u32((uint32_t)(q31_u_d_temp*q31_u_d_temp+q31_u_q_temp*q31_u_q_temp));
//	  arm_sqrt_q31((q31_u_d_temp*q31_u_d_temp+q31_u_q_temp*q31_u_q_temp)<<1,&MS.u_abs);
//	  MS.u_abs = (MS.u_abs>>16)+1;

#ifdef BENCH_BUILD
	  pt_cercle = (MS.u_abs > _U_MAX) ? 1u : 0u;
#endif
	  if (MS.u_abs > _U_MAX){
			MS.u_q = (q31_u_q_temp*_U_MAX)/MS.u_abs; //division!
			MS.u_d = (q31_u_d_temp*_U_MAX)/MS.u_abs; //division!
			MS.u_abs = _U_MAX;
		}
	  else{
			MS.u_q=q31_u_q_temp;
			MS.u_d=q31_u_d_temp;
		}
	  PI_flag=0;

}

void autodetect(void) {
	/* REVUE-PUISSANCE P2 : l'apprentissage (rotation en boucle ouverte, ~5,4 s) ne demarre pas si un defaut est verrouille,
	   et recharge le chien de garde a chaque pas (le chien de garde du portage est court : ~200 ms). */
	if(safety_is_latched()) return;
	timer_primary_output_config(TIMER0,ENABLE);
	ui_8_PWM_ON_Flag=1;
	MS.hall_angle_detect_flag = 0; //set uq to contstant value in FOC.c for open loop control
	q31_rotorposition_absolute = 1 << 31;
	i32_hall_order = 1;//reset hall order
	MS.i_d_setpoint= 200; //set MS.id to appr. 2000mA
	MS.i_q_setpoint= 0;

	for (int i = 0; i < 1080; i++) {
		q31_rotorposition_absolute += one_deg; //drive motor in open loop with steps of 1 deg
		delay_1ms(5);
		fwdgt_counter_reload();
		if(safety_is_latched()) break;   /* defaut pendant l'apprentissage : on arrete la rotation */


		if (ui8_hall_state_old != ui8_hall_state) {
//			printf_("angle: %d, hallstate:  %d, hallcase %d \n",
//					(int16_t) (((q31_rotorposition_absolute >> 23) * 180) >> 8),
//					ui8_hall_state, ui8_hall_case);

			switch (ui8_hall_case) //12 cases for each transition from one stage to the next. 6x forward, 6x reverse
			{
			//6 cases for forward direction
			case 64:
				Hall_64=q31_rotorposition_absolute;
				break;
			case 45:
				Hall_45=q31_rotorposition_absolute;
				break;
			case 51:
				Hall_51=q31_rotorposition_absolute;
				break;
			case 13:
				Hall_13=q31_rotorposition_absolute;
				break;
			case 32:
				Hall_32=q31_rotorposition_absolute;
				break;
			case 26:
				Hall_26=q31_rotorposition_absolute;
				break;

				//6 cases for reverse direction
			case 46:
				Hall_64=q31_rotorposition_absolute;
				break;
			case 62:
				Hall_26=q31_rotorposition_absolute;
				break;
			case 23:
				Hall_32=q31_rotorposition_absolute;
				break;
			case 31:
				Hall_13=q31_rotorposition_absolute;
				break;
			case 15:
				Hall_51=q31_rotorposition_absolute;
				break;
			case 54:
				Hall_45=q31_rotorposition_absolute;
				break;

			} // end case

            transmit_message.tx_data[0] = (int8_t) (((Hall_64 >> 23) * 180) >> 9);//scale q31 angle to -90 .. +90 for 1 Byte representation
            transmit_message.tx_data[1] = (int8_t) (((Hall_45 >> 23) * 180) >> 9);
            transmit_message.tx_data[2] = (int8_t) (((Hall_51 >> 23) * 180) >> 9);
            transmit_message.tx_data[3] = (int8_t) (((Hall_13 >> 23) * 180) >> 9);
            transmit_message.tx_data[4] = (int8_t) (((Hall_32 >> 23) * 180) >> 9);
            transmit_message.tx_data[5] = (int8_t) (((Hall_26 >> 23) * 180) >> 9);
            transmit_message.tx_data[6] = (adc_value[1]>>8)&0xFF;
            transmit_message.tx_data[7] = (adc_value[1])&0xFF;

            /* transmit message */
            transmit_mailbox = can_message_transmit(CAN0, &transmit_message);
            /* waiting for transmit completed */
            timeout = 0xFFFF;
            while((CAN_TRANSMIT_OK != can_transmit_states(CAN0, transmit_mailbox)) && (0 != timeout)){
                timeout--;
            	}
			ui8_hall_state_old = ui8_hall_state;
		}
	}
	timer_channel_output_pulse_value_config(TIMER0,TIMER_CH_0,0);
	timer_channel_output_pulse_value_config(TIMER0,TIMER_CH_1,0);
	timer_channel_output_pulse_value_config(TIMER0,TIMER_CH_2,0);
	delay_1ms(25);
	timer_primary_output_config(TIMER0,DISABLE); //Disable PWM if motor is not turning

	//ui_8_PWM_ON_Flag=0;

    MS.i_d = 0;
    MS.i_q = 0;
    MS.u_d=0;
    MS.u_q=0;
    MS.i_d_setpoint= 0;
    uint32_tics_filtered=1000000;


	if (i8_recent_rotor_direction == 1) {

		i32_hall_order = 1;
	} else {

		i32_hall_order = -1;
	}

	//write_virtual_eeprom();
	p=0;
	MS.hall_angle_detect_flag = 2;

	delay_1ms(20);
   // ui8_KV_detect_flag = 30;


}



void ADC0_1_IRQHandler(void)
{
    /* clear the ADC flag */
	/* REVUE-PUISSANCE P5 : plus de rechargement du chien de garde dans l'interruption (il masquait un blocage de la boucle principale) */
    adc_interrupt_flag_clear(ADC1, ADC_INT_FLAG_EOIC);
    /* read ADC inserted group data register */

    __disable_irq();
    /* CURRENT_SIGN (config.h) : hypothese de signe a verifier au banc, -1 = convention de l'origine AOCI */
    i16_ph1_current = CURRENT_SIGN * (int16_t)adc_inserted_data_read(ADC2, ADC_INSERTED_CHANNEL_0);
    i16_ph2_current = CURRENT_SIGN * (int16_t)adc_inserted_data_read(ADC1, ADC_INSERTED_CHANNEL_0);
    i16_ph3_current = CURRENT_SIGN * (int16_t)adc_inserted_data_read(ADC0, ADC_INSERTED_CHANNEL_0);
#ifdef BENCH_BUILD
    /* correctif F1 : pendant la mesure pont a 50 % (offsets injectes a 0, valeurs brutes), garde avant toute reconstruction */
    if(offset_cal_pont){
    	static uint8_t n_pont = 0;
    	if(abs(CURRENT_SIGN * i16_ph1_current - offset_cal_ph[0]) > CAL_PONT_LIM
    	   || abs(CURRENT_SIGN * i16_ph2_current - offset_cal_ph[1]) > CAL_PONT_LIM
    	   || abs(CURRENT_SIGN * i16_ph3_current - offset_cal_ph[2]) > CAL_PONT_LIM){
    		if(++n_pont >= PH_TRIP_PERIODS) safety_fault(FAULT_OFFSET);
    	} else n_pont = 0;
    }
    /* diagnostic du demarrage du pont : les CAP_N periodes qui suivent MOE = 1 (pwm_enable_safe) */
    if(cap_n < CAP_N){
    	cap_ph[0][cap_n] = CAP_SAT(i16_ph1_current);
    	cap_ph[1][cap_n] = CAP_SAT(i16_ph2_current);
    	cap_ph[2][cap_n] = CAP_SAT(i16_ph3_current);
    	if(cap_trip == 0xFF && safety_is_latched()) cap_trip = cap_n;
    	cap_n++;
    }
    /* PORTE 4, correctif F4 (26/10/2026) : sur le controleur du velo, les trois voies portent un bruit commun (capture du
       demarrage a u_d 0 : mode commun -19..+14 pts, ecart-type 5, part propre a chaque phase <= 5 pts, somme des trois
       jusqu'a 57 pts alors qu'un vrai courant est de somme nulle). La reconstruction ph3 = -(ph1 + ph2) le doublait (41 pts)
       et declenchait le trip a vide ; il faussait aussi i_d / i_q. On retire la moyenne des trois voies. Valable seulement
       si les trois lectures sont justes : en build de banc, CCR <= ~1738 < DYNAMIC_ADC_THRESHOLD (_U_MAX = U_MAX_BENCH,
       u_d de l'outil <= BENCH_OL_UD_MAX), le declenchement reste a TRIGGER_DEFAULT, bras bas fermes pour les trois. Hors
       calibration (valeurs brutes). La capture ci-dessus garde les valeurs avant correction.
       F4 gardee (inc/f4_mode_commun.h) : refusee si une voie est illisible (f4_masque, critere CCR > DYNAMIC_ADC_THRESHOLD
       d'EBiCS), si une valeur brute est en butee, ou si le mode commun depasse F4_MC_MAX ; les courants restent alors
       intacts et le trip voit la lecture (f4_entrees_trip, plus bas). Brut = registre injecte (CURRENT_SIGN x courant)
       + offset injecte (ADC_IOFF0). f4_mes : les trois voies apres F4, avant la reconstruction d'EBiCS. */
    uint8_t f4_m = 0, f4_st = 4u;                  /* f4_st : resultat de F4 pour la capture avant trip (4 = non evaluee) */
    const int16_t f4_av[3] = {i16_ph1_current, i16_ph2_current, i16_ph3_current};   /* avant F4 (capture E20D) */
    if(!offset_cal_active){
    	f4_m = f4_masque;
    	int32_t b1 = CURRENT_SIGN * i16_ph1_current + (int32_t)(ADC_IOFF0(ADC2) & ADC_IOFFX_IOFF);
    	int32_t b2 = CURRENT_SIGN * i16_ph2_current + (int32_t)(ADC_IOFF0(ADC1) & ADC_IOFFX_IOFF);
    	int32_t b3 = CURRENT_SIGN * i16_ph3_current + (int32_t)(ADC_IOFF0(ADC0) & ADC_IOFFX_IOFF);
    	f4_resultat_t r = f4_mode_commun(&i16_ph1_current, &i16_ph2_current, &i16_ph3_current, b1, b2, b3, f4_m);
    	if(r != F4_APPLIQUEE && f4_refus[r] < 0xFFFFu) f4_refus[r]++;
    	f4_st = (uint8_t)r;
    }
    const int16_t f4_mes[3] = {i16_ph1_current, i16_ph2_current, i16_ph3_current};
#endif


	switch (MS.char_dyn_adc_state) //read in according to state
		{
		case 1: //Phase C at high dutycycles, read from A+B directly
			{
				//first reading is correct, do nothing
			}
			break;
		case 2: //Phase A at high dutycycles, read from B+C (A = -B -C)
			{

				//overwrite A with -B-C
				i16_ph1_current = -i16_ph2_current-i16_ph3_current;

			}
			break;
		case 3: //Phase B at high dutycycles, read from A+C (B=-A-C)
			{

				//overwrite B with -A-C
				i16_ph2_current = -i16_ph1_current-i16_ph3_current;
			}
			break;

		case 0: //timeslot too small for ADC
			{
				//do nothing
			}
			break;




		} // end case

    /* A1 porte 4 (revue Fangyan, point 5) : surintensite verifiee APRES reconstruction, a chaque periode PWM, avant tout calcul.
       ph1 et ph2 sont valides dans les trois etats ; ph3 brute peut etre fausse en etat 1 (phase C la plus chargee) et n'y est
       pas reconstruite : la troisieme phase est donc toujours deduite des deux autres (somme des courants nulle).
       PORTE 4 (26/10/2026) : mesure par Rds(on) des MOSFET bas, valable seulement quand le pont commute (echantillon au sommet,
       bras bas fermes). MOE = 0 : noeuds de phase flottants, la valeur lue n'est pas un courant (-870 points le 26/10, defaut 3
       verrouille a chaque demarrage) et aucun courant ne peut passer par le pont. Calibration : offset injecte a 0. */
    if((TIMER_CCHP(TIMER0) & TIMER_CCHP_POEN) && !offset_cal_active){
#ifdef BENCH_BUILD
    	/* F4 : trois voies lisibles -> par phase, la plus grande de la reconstruction et de la mesure (f4_mode_commun.h) */
    	int16_t f4_t[3];
    	f4_entrees_trip(f4_t, i16_ph1_current, i16_ph2_current, f4_mes, f4_m);
    	if(!pt_gel){                              /* capture avant trip : la periode est enregistree avant le test */
    		pt_ph[0][pt_idx] = CAP_SAT(f4_t[0]);
    		pt_ph[1][pt_idx] = CAP_SAT(f4_t[1]);
    		pt_ph[2][pt_idx] = CAP_SAT(f4_t[2]);
    		pt_st[pt_idx] = (uint8_t)(f4_st | (ui8_6step_flag ? 8u : 0u));
    		pt_ang[pt_idx] = 0; pt_ud[pt_idx] = 0; pt_uq[pt_idx] = 0;
    		pt_av[0][pt_idx] = CAP_SAT(f4_av[0]);
    		pt_av[1][pt_idx] = CAP_SAT(f4_av[1]);
    		pt_av[2][pt_idx] = CAP_SAT(f4_av[2]);
    		pt_x[pt_idx] = (uint8_t)((GPIO_ISTAT(GPIOC) >> 6) & 0x07u);   /* b5 pose plus bas si la FOC tourne */
    		pt_k = pt_idx;
    		pt_idx = (uint8_t)((pt_idx + 1u) % PT_N);
    		if(pt_n < PT_N) pt_n++;
    	}
    	safety_check_phase_currents(f4_t[0], f4_t[1], f4_t[2]);
    	if(safety_is_latched()) pt_gel = 1;
#else
    	safety_check_phase_currents(i16_ph1_current, i16_ph2_current, (int16_t)(-(i16_ph1_current + i16_ph2_current)));
#endif
    }
    /* garde : quel que soit le chemin qui aurait remis POEN a 1, un defaut verrouille recoupe dans la periode */
    if(safety_is_latched()) safety_pwm_off();

    //get the recent timer value from the Hall timer
    ui16_tim2_recent = timer_counter_read(TIMER2);
#ifdef BENCH_BUILD
    /* lu APRES le compteur : un front tombe entre les deux ne fait que tenir l'angle une periode (sans danger) */
    uint8_t front_en_attente = (TIMER_INTF(TIMER2) & TIMER_INTF_CH0IF) ? 1u : 0u;
#endif
    if (ui16_tim2_recent>SIXSTEPTHRESHOLD<<1){
    	ui16_timertics=SIXSTEPTHRESHOLD<<1;
    	uint32_tics_filtered=ui16_timertics<<3;
    }
    //check the speed for sixstep threshold
	if (ui16_timertics < SIXSTEPTHRESHOLD && ui16_tim2_recent < 200)
		ui8_6step_flag = 0;
	if (ui16_timertics > (SIXSTEPTHRESHOLD * 6) >> 2)
		ui8_6step_flag = 1;

#ifdef BENCH_BUILD
    uint32_t t_interp = ui16_tim2_recent;          /* borne de l'interpolation : fraction <= 1 */
    if (t_interp > (uint32_tics_filtered >> 3)) {
    	t_interp = uint32_tics_filtered >> 3;
    	if (!ui8_6step_flag && bornes_interp < 0xFFFFu) bornes_interp++;
    }
#endif
    // extrapolate rotorposition from filtered speed reading
    if(MS.hall_angle_detect_flag){//q31_rotorposition_absolute = q31_rotorposition_hall + (q31_t) ((float)(i8_recent_rotor_direction * (deg_30<<1) * ui16_tim2_recent)/(float)(uint32_tics_filtered>>3));//
//Speed PLL not implemented yet.
#ifdef BENCH_BUILD
    	if(front_en_attente){                      /* pepin d'angle : angle de la periode precedente tenu */
    		if(pepins_tenus < 0xFFFFu) pepins_tenus++;
    		pt_tenu = 1;
    	}
    	else
#endif
    	if(!ui8_6step_flag){
    	q31_rotorposition_absolute = q31_rotorposition_hall + MP.angle_correction +
    									+ (q31_t) (i8_recent_rotor_direction
#ifdef BENCH_BUILD
    											* (((int32_t)secteur_q16 * (int32_t)t_interp)   /* largeur reelle, fraction bornee */
#else
    											* ((10923 * ui16_tim2_recent)
#endif
    													/ (uint32_tics_filtered>>3)) << 16);//interpolate angle between two hallevents by scaling timer2 tics, 10923<<16 is 715827883 = 60deg
    	}
    	else q31_rotorposition_absolute = q31_rotorposition_hall - MP.reverse * deg_30; //offset of 30 degree to get the middle of the sector

    }

	//get the Phase with highest duty cycle for dynamic phase current reading
	dyn_adc_state(q31_rotorposition_absolute);

    //q31_rotorposition_absolute=(int16_t)((180.0/75.0)*(float)(1<<31));
    if(ui_8_PWM_ON_Flag && !safety_is_latched()){
#ifdef BENCH_BUILD
		if(bench_ol_active){
			bench_ol_vector(i16_ph1_current, i16_ph2_current, bench_ol_angle, bench_ol_ud, &MS);
		} else
#endif
		FOC_calculation(i16_ph1_current, i16_ph2_current,
					q31_rotorposition_absolute,
					(((int16_t) MP.reverse * i8_reverse_flag)
							* MS.i_q_setpoint), &MS, &MP);

		timer_channel_output_pulse_value_config(TIMER0,TIMER_CH_0,switchtime[0]);
		timer_channel_output_pulse_value_config(TIMER0,TIMER_CH_1,switchtime[1]);
		timer_channel_output_pulse_value_config(TIMER0,TIMER_CH_2,switchtime[2]);
#ifdef BENCH_BUILD
		if(pt_k < PT_N){                          /* capture enrichie : meme periode que les entrees du trip */
			pt_ang[pt_k] = (uint8_t)((uint32_t)q31_rotorposition_absolute >> 24);
			pt_ud[pt_k] = CAP_SAT(MS.u_d / 16);
			pt_uq[pt_k] = CAP_SAT(MS.u_q / 16);
			pt_x[pt_k] |= (uint8_t)(0x20u | (pt_cercle ? 0x10u : 0u) | (pt_tenu ? 0x40u : 0u));
		}
#endif

    }
#ifdef BENCH_BUILD
    pt_k = 0xFF; pt_tenu = 0;
#endif
    __enable_irq();

}

/* A1 porte 4 (revue Fangyan, point 4). Appelee avec ui_8_PWM_ON_Flag = 0 : l'ISR n'ecrit alors pas les CCR.
   PI et tensions a zero, rapports cycliques neutres (50 %), puis test du verrou et POEN = 1 interruptions masquees :
   l'ISR ADC ne peut pas poser un defaut entre le test et l'activation. Si elle en pose un juste apres, elle recoupe (garde ISR). */
static uint8_t pwm_enable_safe(void)
{
	uint8_t ok;
	PI_iq.integral_part=0;
	PI_iq.out=0;
	PI_id.integral_part=0;
	PI_id.out=0;
	MS.u_d=0;
	MS.u_q=0;
	MS.u_abs=0;
	BC_limit_flag=0;
	timer_channel_output_pulse_value_config(TIMER0,TIMER_CH_0,_T>>1);
	timer_channel_output_pulse_value_config(TIMER0,TIMER_CH_1,_T>>1);
	timer_channel_output_pulse_value_config(TIMER0,TIMER_CH_2,_T>>1);
	__disable_irq();
	ok = !safety_is_latched() && offsets_valides;     /* PORTE 4 : jamais MOE sans offsets calibres roue arretee */
#ifdef BENCH_BUILD
	if(ok){
		/* F3 : CHCTL2 = 0, aucune sortie validee : repos OSSR, hauts bloques (CHxP = 0), bas passants (CHxNP = 0), comme
		   offset_cal_phase avec les trois bras. Moteur arrete : aucun courant ; s'il tourne, le trip reste actif. */
		if(bench_precharge_ms) TIMER_CHCTL2(TIMER0) = 0U;
		cap_trip = 0xFF; cap_n = 0; cap_seq++;
		hj_idx = 0; hj_n = 0; hj_seq++; hj_gel = 0;
		pt_idx = 0; pt_n = 0; pt_seq++; pt_gel = 0;
	}
#endif
	if(ok) TIMER_CCHP(TIMER0) |= (uint32_t)TIMER_CCHP_POEN;
	__enable_irq();
#ifdef BENCH_BUILD
	if(ok && bench_precharge_ms){
		for(uint16_t i = 0; i < 4u * bench_precharge_ms; i++){   /* ticks de 4 kHz */
			reg_ADC_flag = 0;
			if(!wait_reg_adc_flag()) break;
		}
		__disable_irq();
		TIMER_CHCTL2(TIMER0) = CHCTL2_RUN;            /* decoupage a 50 % (CCR neutres ci-dessus) */
		ok = !safety_is_latched();
		__enable_irq();
	}
#endif
	return ok;
}

/* A1 porte 4 : MOE = 0 d'abord, puis le drapeau (l'ISR cesse d'ecrire les CCR), puis CCR neutres et integrales a zero.
   Avant : CCR ecrits avant le drapeau, une ISR intercalee pouvait laisser des rapports cycliques non neutres. */
static void pwm_disable(void)
{
	safety_pwm_off();
	ui_8_PWM_ON_Flag=0;
#ifdef BENCH_BUILD
	hj_gel = 1;                                   /* journal des fronts Hall fige a l'arret du pont */
	pt_gel = 1;                                   /* capture avant trip figee a l'arret du pont */
#endif
	timer_channel_output_pulse_value_config(TIMER0,TIMER_CH_0,_T>>1);
	timer_channel_output_pulse_value_config(TIMER0,TIMER_CH_1,_T>>1);
	timer_channel_output_pulse_value_config(TIMER0,TIMER_CH_2,_T>>1);
	PI_iq.integral_part=0;
	PI_id.integral_part=0;
}

int32_t map (int32_t x, int32_t in_min, int32_t in_max, int32_t out_min, int32_t out_max)
{
  // if input is smaller/bigger than expected return the min/max out ranges value
  if (x < in_min)
    return out_min;
  else if (x > in_max)
    return out_max;

  // map the input to the output range.
  // round up if mapping bigger ranges to smaller ranges
  else  if ((in_max - in_min) > (out_max - out_min))
    return (x - in_min) * (out_max - out_min + 1) / (in_max - in_min + 1) + out_min;
  // round down if mapping smaller ranges to bigger ranges
  else
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}


/* REVUE-PUISSANCE P1 : une conversion injectee + stabilisation prend ADC_CONV_TICKS (120) + ADC_SETTLE_TICKS (60) = 180 ticks de 66 MHz (~2,7 us).
   Au-dela de DYNAMIC_ADC_THRESHOLD (1810), le declenchement CC4 suit la phase la plus chargee. */
void dyn_adc_state(q31_t angle){
	if (switchtime[2]>switchtime[0] && switchtime[2]>switchtime[1]){
		MS.char_dyn_adc_state = 1; // -90Ã‚Â° .. +30Ã‚Â°: Phase C at high dutycycles
		if(switchtime[2]>DYNAMIC_ADC_THRESHOLD)timer_channel_output_pulse_value_config(TIMER0,TIMER_CH_3,(switchtime[2]-TRIGGER_OFFSET_ADC));
		else timer_channel_output_pulse_value_config(TIMER0,TIMER_CH_3,TRIGGER_DEFAULT);
#ifdef BENCH_BUILD
		f4_masque = (switchtime[2]>DYNAMIC_ADC_THRESHOLD) ? 0x03u : F4_MASQUE_TOUTES;   /* F4 : ph3 illisible si deplace */
#endif
	}

	if (switchtime[0]>switchtime[1] && switchtime[0]>switchtime[2]) {
		MS.char_dyn_adc_state = 2; // +30Ã‚Â° .. 150Ã‚Â° Phase A at high dutycycles
		if(switchtime[0]>DYNAMIC_ADC_THRESHOLD)timer_channel_output_pulse_value_config(TIMER0,TIMER_CH_3,(switchtime[0]-TRIGGER_OFFSET_ADC));
		else timer_channel_output_pulse_value_config(TIMER0,TIMER_CH_3,TRIGGER_DEFAULT);
#ifdef BENCH_BUILD
		f4_masque = (switchtime[0]>DYNAMIC_ADC_THRESHOLD) ? 0x06u : F4_MASQUE_TOUTES;   /* F4 : ph1 illisible si deplace */
#endif
	}

	if (switchtime[1]>switchtime[0] && switchtime[1]>switchtime[2]){
		MS.char_dyn_adc_state = 3; // +150 .. -90Ã‚Â° Phase B at high dutycycles
		if(switchtime[1]>DYNAMIC_ADC_THRESHOLD)timer_channel_output_pulse_value_config(TIMER0,TIMER_CH_3,(switchtime[1]-TRIGGER_OFFSET_ADC));
		else timer_channel_output_pulse_value_config(TIMER0,TIMER_CH_3,TRIGGER_DEFAULT);
#ifdef BENCH_BUILD
		f4_masque = (switchtime[1]>DYNAMIC_ADC_THRESHOLD) ? 0x05u : F4_MASQUE_TOUTES;   /* F4 : ph2 illisible si deplace */
#endif
	}
}

uint8_t interpolate_assistfactor(void){
	uint16_t interval= speedlimitx100_scaled/5 ;
	uint8_t ui8_speedfactor=0;
	uint8_t ui8_speedcase=0;
	if (MS.Speedx100 < interval)ui8_speedcase=0;
	else if (MS.Speedx100 < 2*interval)ui8_speedcase=1;
	else if (MS.Speedx100 < 3*interval)ui8_speedcase=2;
	else if (MS.Speedx100 < 4*interval)ui8_speedcase=3;
	else ui8_speedcase=4;

	ui8_speedfactor = map(
			MS.Speedx100,
			ui8_speedcase*interval,
			(ui8_speedcase+1)*interval,
			MP.assist_profile[level_to_array_element[MS.assist_level]-1][ui8_speedcase],
			MP.assist_profile[level_to_array_element[MS.assist_level]-1][ui8_speedcase+1]);
	return ui8_speedfactor;
}

void print_debug_on_CAN(void){


	transmit_message.tx_sfid = 0x00;
	transmit_message.tx_efid = 0x00010203; //ID for debug message
	transmit_message.tx_ft = CAN_FT_DATA;
	transmit_message.tx_ff = CAN_FF_EXTENDED;
	transmit_message.tx_dlen = 8;
	transmit_message.tx_data[0] = (temp1>>8)&0xFF;//(GPIO_ISTAT(GPIOC)>>6)&0x07;
	transmit_message.tx_data[1] = (temp1)&0xFF; //ui16_timertics>>8;//(GPIO_ISTAT(GPIOA)>>8)&0xFF;
	transmit_message.tx_data[2] = (temp2>>8)&0xFF;;
	transmit_message.tx_data[3] = (temp2)&0xFF;
	transmit_message.tx_data[4] = (temp3>>8)&0xFF;//
	transmit_message.tx_data[5] = (temp3)&0xFF;
	transmit_message.tx_data[6] = (temp4>>8)&0xFF;
	transmit_message.tx_data[7] = (temp4)&0xFF;

	/* transmit message */
	transmit_mailbox = can_message_transmit(CAN0, &transmit_message);
	/* waiting for transmit completed */
	timeout = 0xFFFF;
	while((CAN_TRANSMIT_OK != can_transmit_states(CAN0, transmit_mailbox)) && (0 != timeout)){
		timeout--;
		}

}

int16_t T_NTC(uint16_t ADC) // ADC 12 Bit, 10k NTC, RÃ¼ckgabewert in Â°C

{
	int R = R_TEMP_PULLUP;                                    // Spannungsteiler, fester Widerstand
	float Rn = 5000;                                         // gemessen (Ohm)
	float Tn = 23;                                            // gemessen (°C)
	float B = 3398;
    float U_ntc = (3.3 * ADC) / 4095;             // Spannung
    float R_ntc = (U_ntc * R) / (3.3 - U_ntc);           // Widerstand
                                                          // Temperatur-Berechnung
        // Rt = Rn * e hoch B*(1/T - 1/Tn)                // Ausgangsformel
        // T = 1 / [(log(Rt/Rn)/B + 1/Tn] - 273,15        // umgestellt in °K
        // Tnk = 26,2 + 273,15                            // °K
    float A1 = log(R_ntc / Rn) / B;
    float A2 = A1 + 1 / (Tn + 273.15);
    float T = (1 / A2) - 273.15;
	return (int)T; // Rundung

}

void get_standstill_position(){

	  delay_1ms(25);
	  ui8_hall_state = (GPIO_ISTAT(GPIOC)>>6)&0x07;
#ifdef BENCH_BUILD
	/* Porte 4, 27/10/2026 (walk 3 pts apres mise sous tension : a-coup puis arret) : ui8_hall_state_old vaut 0 tant
	   qu'aucun front n'a ete vu ; le premier front donnait le cas 0y, absent du switch de TIMER2_IRQHandler : angle et sens
	   non mis a jour, le rotor se calait avant le front suivant. L'etat a l'arret devient l'etat precedent. */
	if (ui8_hall_state >= 1u && ui8_hall_state <= 6u) ui8_hall_state_old = ui8_hall_state;
	/* Porte 4, 27/10/2026 : a l'arret le 6-step applique hall - MP.reverse*30 deg. Avec MP.reverse = 1 (couple vers theta
	   decroissant), on entre dans le secteur par son bord HAUT : prendre ce bord pour viser le milieu du secteur
	   (simulation : couple avant >= 83 % sur les 6 etats, contre 47 % en moyenne et ~0 pres du bord avec le bord bas). */
	if (MP.reverse > 0) {
		switch (ui8_hall_state) {
			case 2: q31_rotorposition_hall = Hall_26; break;
			case 6: q31_rotorposition_hall = Hall_64; break;
			case 4: q31_rotorposition_hall = Hall_45; break;
			case 5: q31_rotorposition_hall = Hall_51; break;
			case 1: q31_rotorposition_hall = Hall_13; break;
			case 3: q31_rotorposition_hall = Hall_32; break;
		}
		q31_rotorposition_absolute = q31_rotorposition_hall;
		return;
	}
#endif
		switch (ui8_hall_state) {
			//6 cases for forward direction
			case 2:
				q31_rotorposition_hall = Hall_32;
				break;
			case 6:
				q31_rotorposition_hall = Hall_26;
				break;
			case 4:
				q31_rotorposition_hall = Hall_64;
				break;
			case 5:
				q31_rotorposition_hall = Hall_45;
				break;
			case 1:
				q31_rotorposition_hall = Hall_51;

				break;
			case 3:
				q31_rotorposition_hall = Hall_13;
				break;

			}

			q31_rotorposition_absolute = q31_rotorposition_hall;
}

int8_t calculate_SOC(uint16_t voltage, uint8_t cells_in_series){ //interpolate from lookup table
    if (cells_in_series == 0) return 0; /* protection division par zero (SYSTEM_VOLTAGE=0 en EEPROM non initialisee -> +Inf -> boucle sur NaN) */
    float voltages[] = {2.9, 3.15, 3.30, 3.42, 3.55, 3.60, 3.65, 3.70, 3.75, 3.80, 3.85, 3.90, 4.00, 4.10, 4.20};
    float soc_values[] = {0, 12, 22, 32, 44, 48, 55, 60, 68, 72, 77, 88, 95, 97, 100};
    int length = sizeof(voltages) / sizeof(voltages[0]);
    float cell_voltage = (float)voltage/((float)cells_in_series*1000);
    if (cell_voltage <= voltages[0]) {
        return (int8_t)soc_values[0];
    }
    if (cell_voltage >= voltages[length - 1]) {
        return (int8_t)soc_values[length - 1];
    }

    for (int i = 0; i < length - 1; i++) {
        if (cell_voltage < voltages[i+1]) {
            float slope = (soc_values[i+1] - soc_values[i]) / (voltages[i+1] - voltages[i]);
            float soc = soc_values[i] + slope * (cell_voltage - voltages[i]);
            return (int8_t)soc;
        }
    }
    return (int8_t)soc_values[length - 1];
}

/*!
    \brief      erase fmc pages from FMC_WRITE_START_ADDR to FMC_WRITE_END_ADDR
    \param[in]  none
    \param[out] none
    \retval     none
*/
void fmc_erase_pages(void)
{
    uint32_t EraseCounter;

    /* unlock the flash program/erase controller */
    fmc_unlock();

    /* clear all pending flags */
    fmc_flag_clear(FMC_FLAG_BANK0_END);
    fmc_flag_clear(FMC_FLAG_BANK0_WPERR);
    fmc_flag_clear(FMC_FLAG_BANK0_PGERR);

    /* erase the flash pages */
    for(EraseCounter = 0; EraseCounter < PageNum; EraseCounter++){
        fmc_page_erase(FMC_WRITE_START_ADDR + (FMC_PAGE_SIZE * EraseCounter));
        fmc_flag_clear(FMC_FLAG_BANK0_END);
        fmc_flag_clear(FMC_FLAG_BANK0_WPERR);
        fmc_flag_clear(FMC_FLAG_BANK0_PGERR);
    }

    /* lock the main FMC after the erase operation */
    fmc_lock();
}

/*!
    \brief      program fmc word by word from FMC_WRITE_START_ADDR to FMC_WRITE_END_ADDR
    \param[in]  none
    \param[out] none
    \retval     none
*/
void fmc_program_hall_angles(void)
{
    /* unlock the flash program/erase controller */
    fmc_unlock();

    address = FMC_WRITE_START_ADDR;

    /* program flash */

        fmc_word_program(address, (uint32_t)i32_hall_order);
        address += 4;
        fmc_flag_clear(FMC_FLAG_BANK0_END);
        fmc_flag_clear(FMC_FLAG_BANK0_WPERR);
        fmc_flag_clear(FMC_FLAG_BANK0_PGERR);

        fmc_word_program(address, (uint32_t)Hall_13);
        address += 4;
        fmc_flag_clear(FMC_FLAG_BANK0_END);
        fmc_flag_clear(FMC_FLAG_BANK0_WPERR);
        fmc_flag_clear(FMC_FLAG_BANK0_PGERR);

        fmc_word_program(address, (uint32_t)Hall_32);
        address += 4;
        fmc_flag_clear(FMC_FLAG_BANK0_END);
        fmc_flag_clear(FMC_FLAG_BANK0_WPERR);
        fmc_flag_clear(FMC_FLAG_BANK0_PGERR);

        fmc_word_program(address, (uint32_t)Hall_26);
        address += 4;
        fmc_flag_clear(FMC_FLAG_BANK0_END);
        fmc_flag_clear(FMC_FLAG_BANK0_WPERR);
        fmc_flag_clear(FMC_FLAG_BANK0_PGERR);

        fmc_word_program(address, (uint32_t)Hall_64);
        address += 4;
        fmc_flag_clear(FMC_FLAG_BANK0_END);
        fmc_flag_clear(FMC_FLAG_BANK0_WPERR);
        fmc_flag_clear(FMC_FLAG_BANK0_PGERR);

        fmc_word_program(address, (uint32_t)Hall_45);
        address += 4;
        fmc_flag_clear(FMC_FLAG_BANK0_END);
        fmc_flag_clear(FMC_FLAG_BANK0_WPERR);
        fmc_flag_clear(FMC_FLAG_BANK0_PGERR);

        fmc_word_program(address, (uint32_t)Hall_51);
        address += 4;
        fmc_flag_clear(FMC_FLAG_BANK0_END);
        fmc_flag_clear(FMC_FLAG_BANK0_WPERR);
        fmc_flag_clear(FMC_FLAG_BANK0_PGERR);


    /* lock the main FMC after the program operation */
    fmc_lock();
}

fmc_state_enum fmc_multi_word_program(uint32_t offset, uint8_t* data, uint8_t words)
{
	uint32_t temp=0;
	fmc_state_enum returnvalue;
	uint32_t target_address = FMC_WRITE_START_ADDR+offset;
    fmc_unlock();
            	for (k=0; k < words; k++){
            		memcpy(&temp, data+k*4,4);
            		returnvalue = fmc_word_program(target_address, (uint32_t)temp);
                    target_address += 4;
                    fmc_flag_clear(FMC_FLAG_BANK0_END);
                    fmc_flag_clear(FMC_FLAG_BANK0_WPERR);
                    fmc_flag_clear(FMC_FLAG_BANK0_PGERR);
            	}


     fmc_lock();
    return returnvalue;
}

void write_virtual_eeprom(void)
	{
#ifdef BENCH_NO_EEPROM
		return; /* build banc : jamais d'effacement ni d'ecriture de la page (0x6101, 0x62D9, 0x3203, multitrame, fin d'apprentissage) */
#endif
		fmc_erase_pages();
		fmc_program_hall_angles();
		fmc_multi_word_program(FMC_OFFSET_MP, (uint8_t*)&MP, (sizeof(MP)+3)/4); //Did not know padding yet :-)
	}

void read_virtual_eeprom(void)
	{
#ifdef BENCH_NO_EEPROM
		return; /* build banc : MP et angles Hall viennent uniquement du build */
#endif
    //read individual hall angles from virtual EEPROM
    ptrd = (uint32_t *)FMC_WRITE_START_ADDR;
    if(0xFFFFFFFF != (*(ptrd+1))){
    	i32_hall_order=(int32_t)(*ptrd);
    	ptrd++;
    	Hall_13 = (int32_t)(*ptrd);
    	ptrd++;
    	Hall_32 = (int32_t)(*ptrd);
    	ptrd++;
    	Hall_26 = (int32_t)(*ptrd);
    	ptrd++;
    	Hall_64 = (int32_t)(*ptrd);
    	ptrd++;
    	Hall_45 = (int32_t)(*ptrd);
    	ptrd++;
    	Hall_51 = (int32_t)(*ptrd);
    	ptrd++;
    }

     memcpy(&MP,(uint32_t *)(FMC_WRITE_START_ADDR+FMC_OFFSET_MP),sizeof(MP));
	}

uint16_t map_rezi(int32_t actual_value, int32_t actual_time, int32_t timeout, int32_t decay_base){
    if(actual_time<timeout)return actual_value;
    else if(actual_time<4000) return (uint16_t)((float)actual_value/((1+(float)(actual_value*decay_base)/175000*(float)(actual_time-timeout))));
    else return 0;
}

uint16_t update_setpoint(void){

				//calculate iq setpoint
#ifdef BENCH_BUILD
	            /* PORTE 4, home trainer (avis Fangyan 26/10) : seule assistance possible, le walk assist lance par CAN (WALK_START,
	               heartbeat obligatoire, bench_can.c), plafonne a bench_walk_ilim <= BENCH_WALK_ILIM_MAX. Couple, accelerateur et
	               bouton walk de l'afficheur sont ignores. Les reductions (batterie, temperature, sonde) s'appliquent ensuite. */
	            if(1){                       /* les branches d'origine (else if ...) ne s'executent jamais en build de banc */
	            	if(MS.brake_active_flag || !bench_walk) MS.i_q_setpoint_temp=0;
	            	else MS.i_q_setpoint_temp=bench_walk_bride(   /* bride en erps (decision du 27/10), MS.Speedx100 intact */
	            		map(MS.Speedx100, (int32_t)MP.walk_assist_speed-200, MP.walk_assist_speed, bench_walk_ilim, 0),
	            		bench_erps_bride(ui16_erps, ui16_timertics, SIXSTEPTHRESHOLD));
	            }
#else
	            //check brake with first priority
	            if(MS.brake_active_flag)MS.i_q_setpoint_temp=0;
#endif
	            // check push assist active
	            else if(MS.pushassist_flag){
	            	MS.i_q_setpoint_temp=map(MS.Speedx100, (int32_t)MP.walk_assist_speed-200, MP.walk_assist_speed, MP.phase_current_max*MP.walk_assist_current/100, 0);
	            }
	            //calculate setpoint, if brake is not activated
	            else{
					mapped_throttle= map(adc_value[1], MP.throttle_offset, MP.throttle_max, 0, phase_current_max_scaled);
					mapped_torque= map(MS.torque_on_crank, MP.TQO_threshold[level_to_array_element[MS.assist_level]], 3300, 0, phase_current_max_scaled);

					if(Backwards_counter<4){//normal ride mode, motor power only if pedals are not turned backwards
						MS.i_q_setpoint_temp=(uint32_t)((float) (MP.TS_coeff*powf((float)MS.cadence,helper))*(MS.torque_filtered)*0.0005*interpolate_assistfactor());//factor 0.0005 from various constants
						//limit setpoint to the max value according to the current setting.
						if(MS.i_q_setpoint_temp>phase_current_max_scaled)MS.i_q_setpoint_temp = phase_current_max_scaled;
						MS.i_q_setpoint_temp=map_rezi(MS.i_q_setpoint_temp, torque_counter, MP.PAS_timeout, MP.decay_base);


						//torque override
						if(mapped_torque>MS.i_q_setpoint_temp){
							if(mapped_torque>Overrun_strength){
								Overrun_strength=mapped_torque;
								Overrun_counter = 0;
							}
							MS.i_q_setpoint_temp=mapped_torque;
						}
					}
					else MS.i_q_setpoint_temp=0; //cut motor power on pedaling backwards
						//throttle override
					if(mapped_throttle>MS.i_q_setpoint_temp)MS.i_q_setpoint_temp=mapped_throttle;

					//apply Extended Boost
					if(Overrun_counter<(MP.Override_Duration*MS.ext_boost_duration)/100&&Backwards_counter<4){
						MS.i_q_setpoint_temp=(Overrun_strength*MS.ext_boost_strength)/100;
						if(MS.i_q_setpoint_temp>MP.phase_current_max)MS.i_q_setpoint_temp = MP.phase_current_max;
						Overrun_flag=1;
						torque_counter=MP.PAS_timeout+1;
					}
					else {
						Overrun_strength=0;
						Overrun_flag=0;
					}


	            }// else brake not active
	        	//if(PAS_counter>MP.PAS_timeout)MS.i_q_setpoint_temp=0;

	            //low battery ramp down with 3V above battery min voltage
	            MS.i_q_setpoint_temp=map(voltage_raw_filtered, MP.voltage_min,(MP.voltage_min+176),0,MS.i_q_setpoint_temp);

	            //motor temperature ramp down between 110 and 130°C
	            /* REVUE-PUISSANCE P9 : reduction progressive du courant de 85 a 100 degres C (100 % -> 33 %), comme le firmware AOCI
	               d'origine sur la sonde du controleur (EBiCS : 110 a 130 degres, valeurs d'un moteur de moyeu Bafang).
	               ATTENTION : EBiCS lit PB1 (adc_value[6]) ; sur AOCI PB1 est la sonde du CONTROLEUR, PB0 celle du moteur.
	               Sonde absente ou hors plage (ADC > 4000 ou < 50) : pas d'assistance, comme le defaut 15 de l'origine. */
	            MS.i_q_setpoint_temp=map(MS.int_Temperature,85,100,MS.i_q_setpoint_temp,MS.i_q_setpoint_temp/3);
#if (TEMP_SENSOR_REQUIRED)
	            if(adc_value[6]>4000||adc_value[6]<50){
	            	MS.i_q_setpoint_temp=0;
	            }
#endif
	            if(MP.legalflag&&!MS.offroadflag){

					if((uint16_cadence_filtered>>3)>15){
						MS.i_q_setpoint_temp=map(MS.Speedx100, speedlimitx100_scaled,(speedlimitx100_scaled+200),MS.i_q_setpoint_temp,0);
					}
					else{ //limit to 6km/h if pedals are not turning
						MS.i_q_setpoint_temp=map(MS.Speedx100, 500,700,MS.i_q_setpoint_temp,0);
					}

				}//end legalflag
				if(MS.hall_angle_detect_flag>1){ // part 2 of positions calibration
					MS.i_q_setpoint_temp=100;
					temp6-=temp6>>4;
					temp6+=MS.u_d;
					if (p>70){
						p=60;
						if ((MP.reverse*temp6>>4)>100)MP.angle_correction+=one_deg;
						else if ((MP.reverse*temp6>>4)<-50)MP.angle_correction-=one_deg;
						else {
							MS.i_q_setpoint_temp=0;
							PI_iq.integral_part=0;
							PI_id.integral_part=0;
							timer_channel_output_pulse_value_config(TIMER0,TIMER_CH_0,_T>>1);
							timer_channel_output_pulse_value_config(TIMER0,TIMER_CH_1,_T>>1);
							timer_channel_output_pulse_value_config(TIMER0,TIMER_CH_2,_T>>1);
							timer_primary_output_config(TIMER0,DISABLE); //Disable PWM
							write_virtual_eeprom();
							MS.hall_angle_detect_flag=1;
						}
					}

				}
				if(!MS.i_q_setpoint_temp&&PI_iq.integral_part){
					PI_iq.integral_part=0;
					PI_id.integral_part=0;
				}


	/* coupe aussi MOE = 0 dans la boucle lente des que la consigne retombe a zero */
#ifdef BENCH_BUILD
	if(!MS.i_q_setpoint_temp && ui_8_PWM_ON_Flag && !bench_ol_active){   /* l'outil vecteur fixe gere sa propre coupure */
#else
	if(!MS.i_q_setpoint_temp && ui_8_PWM_ON_Flag){
#endif
		pwm_disable();
	}
	return MS.i_q_setpoint_temp;

}


#ifdef GD_ECLIPSE_GCC
/* retarget the C library printf function to the USART, in Eclipse GCC environment */
int __io_putchar(int ch)
{
    usart_data_transmit(UART4, (uint8_t)ch);
    while(RESET == usart_flag_get(UART4, USART_FLAG_TBE));

    return ch;
}
#else
/* retarget the C library printf function to the USART */
int fputc(int ch, FILE *f)
{
    usart_data_transmit(UART4, (uint8_t)ch);
    while(RESET == usart_flag_get(UART4, USART_FLAG_TBE));

    return ch;
}
#endif /* GD_ECLIPSE_GCC */

