/*
 * bench_can.c : télémétrie CAN de banc, émission seule (voir bench_can.h). Compilé uniquement en build de banc.
 *
 * Trames (index, sub) et cadences :
 *   E000 50 Hz  i_q consigne, i_q, i_d (i16, pts), drapeaux (b0 MOE, b1 PWM_ON, b2 walk, b3 frein, b4 heartbeat (0 en T1),
 *               b5 défaut verrouillé, b6 outil vecteur fixe actif, b7 offsets de courant dans la plage)
 *   E001 50 Hz  u_d, u_q (i16), u_abs (u16), Hall (b0-2 état GPIOC PC6-8, b3-5 ui8_hall_state)
 *   E002 20 Hz  outil vecteur fixe : actif u8, u_d i16, angle u16 (q31 >> 16), keepalive u16 (ticks 4 kHz)
 *   E100 20 Hz  ph1, ph2, ph3 efficaces (u16, 0,1 pt) et crête (u8, pt, saturé) sur 50 ms à 4 kHz
 *   E101 20 Hz  Vbat (u16, 0,01 V), Ibat (i16, 0,01 A), Vbat brute (u16, pt), SOC (u8, %)
 *   E102 20 Hz  vitesse (u16, 0,01 km/h), erps (u16), cadence (u8, tr/min), couple brut (u16, mV)
 *   E103 20 Hz  couple filtré (u16), niveau (u8), walk par CAN (u8), consigne amont i_q_setpoint_temp (i16, pts),
 *               ilim du walk (u8, pts)
 *   E200 10 Hz  défaut (u8), défaut banc (u8, 0 en T1), état (b0 MOE, b1 PWM_ON, b2 outil), Hall brut (u8),
 *               half_rotation_counter (u16), cause du reset (u8, RCU_RSTSCK >> 24)
 *   E201 10 Hz  T contrôleur (i16, °C), T moteur (i16, 0x7FFF = non mesurée : PB0 non lue), ADC PB1 (u16), pile (u8, 0xFF = non mesurée)
 *   E202 10 Hz  trames perdues (u16), réceptions perdues (u8, 0 en T1), TEC (u8), REC (u8), cycles max d'un envoi (u16)
 *   E203  1 Hz  + au démarrage (hello) : version (u16), hash git court (u32), macros banc (u8 : b0 BENCH_NO_EEPROM,
 *               b1 TEMP_SENSOR_REQUIRED, b2 CURRENT_SIGN < 0, b3 arbre git modifié)
 *   E204  1 Hz  PH_CURRENT_MAX, PH_CURRENT_TRIP, ilim (u8, pts), _U_MAX / 8 (u8), VOLTAGE_MIN (u16, pt), BENCH_OL_UD_MAX (u8)
 *   E205  1 Hz  offsets bras bas fermé ph1, ph2, ph3 (u16, pt), état (b0 dans la plage, b1 offsets pont à 50 % appliqués,
 *               b2 calibration
 *               faite (MOE autorisé), b3 roue arrêtée (Hall stable 50 ms et vitesse nulle), b4 état Hall valide 1 à 6)
 *   E207  1 Hz  correctifs F1 / F2 : variante 0x0803D800 (u8, 0xFF = vierge), offsets pont à 50 % ph1, ph2, ph3 (u16, pt,
 *               0 = pas encore mesurés). E204 donne PH_CURRENT_MAX et PH_CURRENT_TRIP à l'échelle de la variante.
 *   E208 50 Hz  capture du demarrage du pont : rang i (u8), puis ph1, ph2, ph3 (i8, pts) aux periodes i et i + 1 ; i parcourt
 *               0, 2, ..., CAP_N - 2 (tableau complet en 1,28 s). Courants apres offset, avant reconstruction.
 *   E209  1 Hz  capture : rang atteint (u8, CAP_N = finie), periode ou le defaut verrouille est vu (u8, 0xFF aucun),
 *               precharge (u8, ms), numero de capture (u8), CHCTL2 (u16), CAP_N (u8)
 *   E20A 50 Hz  journal des fronts Hall : rang r (u8, 0 = le plus ancien, 0xFF = journal vide), cas EBiCS xy (u8),
 *               duree depuis le front precedent (u16, ticks de 2 us), drapeaux (u8 : b0 6-step, b1 sens +, b2 sens -,
 *               b3 PWM actif), angle de secteur retenu (u8, 360/256 deg), numero du journal (u8)
 *   E20B 50 Hz  capture avant trip : rang r (u8, 0 = la plus ancienne periode, 0xFF = capture vide), entrees du trip ph1,
 *               ph2, ph3 (i8, pts, saturees a +-127), etat (u8 : b0-1 resultat F4, b2 F4 non evaluee, b3 6-step),
 *               numero de capture (u8), drapeaux (u8 : b0 figee, b1 pleine = PT_N periodes)
 *   E20C 50 Hz  capture enrichie, meme rang que E20B : rang r (u8, 0xFF = vide), u_d / 16 (i8), u_q / 16 (i8), angle
 *               applique (u8, 360/256 deg), octet (b0-2 etat Hall, b4 limitation en cercle, b5 FOC calculee,
 *               b6 angle tenu : front Hall en attente, pepin evite), numero de capture (u8), drapeaux (u8 : b0 figee,
 *               b1 pleine)
 *   E20E  1 Hz  relecture a l'execution (build route, decision du 27/10) : MP.phase_current_max (u8, pts), seuil du trip
 *               safety_ph_trip (u16, pts), MP.reverse (i8), limite de vitesse MP.speedLimitx100 / 100 (u8, km/h), niveau
 *               d'assistance maximal (u8, MS.assist_level ; 0 = pas de borne), MS.assist_level courant (u8)
 *   E20F 10 Hz  diagnostic du pedalier (build route seulement, 27/10) : niveaux (u8 : b0 PC12, b1 PD2), fronts PC12 (u16),
 *               fronts PD2 (u16), interruptions EXTI12 (u16) ; compteurs depuis le demarrage, sur 16 bits
 *   E210 50 Hz  journal des periodes de PD2 (build route seulement, 27/10) : rang r (u8, 0 = la plus ancienne, 0xFF = vide),
 *               periode depuis le front descendant valide precedent (u16, ticks de 4 kHz), duree haute qui precede
 *               (u16, ticks), nombre d'entrees (u8, <= 64), numero d'impulsion (u8)
 *   E211 10 Hz  zero de couple adaptatif (build route seulement, 27/10) : zero (u16, mV), derniere valeur entree (u16, mV),
 *               nombre de valeurs reunies (u8, <= 20)
 *   E212  1 Hz  frequence reelle de reg_ADC_processing (build route seulement, 27/10) : debordements de TIMER1 par seconde
 *               (u16), appels de reg_ADC_processing par seconde (u16), tours de boucle par seconde (u16, sature), part
 *               PWM actif (u8, ms / 4)
 *   E20D 50 Hz  capture avant F4, meme rang que E20B : rang r (u8, 0xFF = vide), ph1, ph2, ph3 apres offset et avant F4
 *               (i8, pts, satures a +-127), numero de capture (u8), drapeaux (u8 : b0 figee, b1 pleine)
 *   E206  sur commande : opcode (u8), cmd_seq (u8), statut (u8 : 0 accepté, 1 refusé sans heartbeat depuis 500 ms,
 *               2 refusé défaut verrouillé, 3 refusé frein ou demande d'assistance, 4 commande invalide,
 *               5 refusé offsets non calibrés : roue jamais arrêtée depuis le démarrage, 6 refusé angles du codeur non
 *               relevés (bench_hall_angles.h absent) ou état Hall invalide, 7 refusé : autre essai en cours (outil / walk))
 *
 * Commandes (T2), reçues par 2 filtres en liste exacte vers la FIFO0, lues ici depuis la boucle principale (aucune
 * interruption ajoutée). Règles :
 *   - ESTOP 0x1DF0E000 (tout contenu) : toujours accepté, défaut 31 verrouillé (MOE = 0 d'abord), remise sous tension.
 *   - HEARTBEAT 0x1DF0E001 : « présent » si reçu il y a 300 ms au plus. Pendant un essai lancé par CAN, chaque heartbeat
 *     remet le keepalive de l'outil à 300 ms (1200 ticks à 4 kHz) : sans heartbeat, l'outil s'arrête de toute façon.
 *   - Heartbeat absent plus de 300 ms pendant un essai lancé par CAN : défaut 30 verrouillé (MOE = 0 d'abord).
 *   - OL_SET (0x10, angle u16, u_d u8) : refusé sans heartbeat continu depuis 500 ms, avec un défaut verrouillé, le frein
 *     serré ou une demande d'assistance ; u_d borné à BENCH_OL_UD_MAX ici et dans bench_ol_vector(). OL_STOP (0x11) :
 *     arrêt de l'outil, sans défaut. L'outil piloté par SWD (keepalive 8000) n'est pas concerné par le heartbeat.
 *   - WALK_START (0x20, ilim u8) : mêmes refus que OL_SET, plus angles du codeur relevés et état Hall valide ; ilim borné
 *     à BENCH_WALK_ILIM_MAX et PH_CURRENT_MAX. Seule assistance du build de banc (update_setpoint). Perte du heartbeat
 *     pendant le walk : défaut 30. WALK_STOP (0x21) : consigne à 0, sans défaut. ESTOP coupe aussi le walk.
 */
#include "bench_can.h"

#ifdef BENCH_BUILD
#include "main.h"
#include "config.h"
#include "FOC.h"
#include "safety.h"
#ifdef ROUTE_BUILD
#include "pedalier_diag.h"
#include "pedalier_pd2.h"
#include "couple_zero.h"
#include "freq_diag.h"
#endif
#if __has_include("bench_hall_angles.h")
#include "bench_hall_angles.h"          /* angles du codeur relevés (A5) : définit BENCH_HALL_MESURE */
#endif

#ifndef BENCH_GIT_HASH
#define BENCH_GIT_HASH 0u
#endif
#ifndef BENCH_GIT_DIRTY
#define BENCH_GIT_DIRTY 0
#endif

#define BENCH_ID(idx, sub) (0x1EE80000u | ((uint32_t)(idx) << 8) | (uint32_t)(sub))

/* état de la commande moteur, défini dans main.c */
extern MotorState_t MS;
extern MotorParams_t MP;
extern PI_control_t PI_iq;
extern int16_t i16_ph1_current, i16_ph2_current, i16_ph3_current;
extern volatile uint16_t voltage_raw_filtered;
extern uint16_t ui16_erps;
extern uint8_t ui8_hall_state;
extern volatile uint16_t uint16_half_rotation_counter;
extern volatile uint8_t ui_8_PWM_ON_Flag;
extern volatile uint16_t adc_value[9];
extern uint32_t boot_reset_flags;
extern volatile int16_t offset_cal_ph[3];
extern volatile uint8_t offsets_valides;
extern volatile uint16_t hall_stable_ticks;
extern volatile int16_t offset_pont_ph[3];
extern volatile uint8_t bench_variante, variante_repli;
#ifdef ROUTE_BUILD
extern pedalier_diag_t pedalier_diag;
extern pd2_journal_t pd2_journal;
extern couple_zero_t couple_zero;
extern freq_diag_t freq_diag;
extern volatile uint16_t pedalier_exti12;
#endif
extern uint8_t bench_ph_max, bench_walk_max;
extern volatile int8_t cap_ph[3][CAP_N];
extern volatile uint8_t cap_n, cap_trip, cap_seq, bench_precharge_ms;
extern volatile uint8_t hj_case[HJ_N], hj_fl[HJ_N], hj_ang[HJ_N];
extern volatile uint16_t hj_tics[HJ_N];
extern volatile uint8_t hj_idx, hj_n, hj_gel, hj_seq;
extern volatile int8_t pt_ph[3][PT_N];
extern volatile uint8_t pt_st[PT_N];
extern volatile uint8_t pt_idx, pt_n, pt_gel, pt_seq;
extern volatile int8_t pt_ud[PT_N], pt_uq[PT_N];
extern volatile uint8_t pt_ang[PT_N], pt_x[PT_N];
extern volatile int8_t pt_av[3][PT_N];
extern volatile uint8_t bench_ol_enable;
extern volatile uint8_t bench_ol_active;
extern volatile int16_t bench_ol_ud;
extern volatile q31_t bench_ol_angle;
extern volatile uint16_t bench_ol_keepalive;

volatile uint32_t bench_ms = 0;

#define ID_ESTOP      0x1DF0E000u
#define ID_HEARTBEAT  0x1DF0E001u
#define ID_CMD        0x1DF0E002u
#define HB_TIMEOUT_MS 300u
#define HB_ARMEMENT_MS 500u
#define OL_KEEPALIVE_CAN 1200u                           /* 300 ms à 4 kHz */
#define OP_OL_SET  0x10u
#define OP_OL_STOP 0x11u
#define OP_WALK_START 0x20u
#define OP_WALK_STOP  0x21u
#define OP_PRECHARGE  0x30u                              /* duree de precharge F3 (u8, ms, 0 a PRECHARGE_MAX_MS) */

static uint8_t hb_vu = 0, ol_par_can = 0, defaut_banc = 0, rx_perdues = 0;
volatile uint8_t bench_walk = 0, bench_walk_ilim = 0;
static uint32_t hb_dernier = 0, hb_debut = 0;
static uint8_t ack_attente = 0, ack_op = 0, ack_seq = 0, ack_statut = 0, ack_tx_seq = 0;

typedef struct { uint8_t idx, sub; uint16_t periode_ms; uint32_t dernier; uint8_t seq; } bench_trame_t;

static bench_trame_t trames[] = {
    {0xE0, 0x00,   20, 0, 0}, {0xE0, 0x01,   20, 0, 0}, {0xE0, 0x02,   50, 0, 0},
    {0xE1, 0x00,   50, 0, 0}, {0xE1, 0x01,   50, 0, 0}, {0xE1, 0x02,   50, 0, 0}, {0xE1, 0x03, 50, 0, 0},
    {0xE2, 0x00,  100, 0, 0}, {0xE2, 0x01,  100, 0, 0}, {0xE2, 0x02,  100, 0, 0},
    {0xE2, 0x03, 1000, 0, 0}, {0xE2, 0x04, 1000, 0, 0}, {0xE2, 0x05, 1000, 0, 0}, {0xE2, 0x07, 1000, 0, 0},
    {0xE2, 0x08,   20, 0, 0}, {0xE2, 0x09, 1000, 0, 0}, {0xE2, 0x0A, 20, 0, 0},
    {0xE2, 0x0B,   20, 0, 0}, {0xE2, 0x0C,   20, 0, 0},
    {0xE2, 0x0D,   20, 0, 0}, {0xE2, 0x0E, 1000, 0, 0},
#ifdef ROUTE_BUILD
    {0xE2, 0x0F,  100, 0, 0}, {0xE2, 0x10,   20, 0, 0}, {0xE2, 0x11,  100, 0, 0}, {0xE2, 0x12, 1000, 0, 0},
#endif
};
#define NB_TRAMES (sizeof(trames) / sizeof(trames[0]))

static uint16_t lost = 0, cycles_max = 0;
static uint8_t prochaine = 0;
static uint8_t cap_tx = 0;                               /* prochain rang envoye par E208 */
static uint8_t hj_tx = 0;                                /* prochain rang envoye par E20A */
static uint8_t pt_tx = 0;                                /* prochain rang envoye par E20B */
static uint8_t pc_tx = 0;                                /* prochain rang envoye par E20C */
static uint8_t pa_tx = 0;                                /* prochain rang envoye par E20D */
#ifdef ROUTE_BUILD
static uint8_t pj_tx = 0;                                /* prochain rang envoye par E210 */
#endif
/* courants de phase : sommes des carrés et crête sur la fenêtre de E100 (200 échantillons à 4 kHz) */
static uint32_t somme2[3] = {0, 0, 0};
static uint16_t n_ech = 0;
static uint8_t crete = 0;

static void put16(uint8_t* d, int32_t v) { d[0] = (uint8_t)v; d[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t* d, uint32_t v) { put16(d, (int32_t)(v & 0xFFFF)); put16(d + 2, (int32_t)(v >> 16)); }
static int32_t sat16(int32_t v) { return v > 32767 ? 32767 : (v < -32768 ? -32768 : v); }
static uint32_t usat16(int32_t v) { return v < 0 ? 0 : (v > 65535 ? 65535 : (uint32_t)v); }

static uint32_t isqrt32(uint32_t x)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > x) b >>= 2;
    while (b) {
        if (x >= r + b) { x -= r + b; r = (r >> 1) + b; } else r >>= 1;
        b >>= 2;
    }
    return r;
}

static uint8_t offsets_ok(void)
{
    for (int i = 0; i < 3; i++)
        if (offset_cal_ph[i] < 2048 - 128 || offset_cal_ph[i] > 2048 + 128) return 0;
    return 1;
}

static uint8_t moe(void) { return (TIMER_CCHP(TIMER0) & TIMER_CCHP_POEN) ? 1u : 0u; }

static uint8_t hb_present(uint32_t now) { return hb_vu && (uint32_t)(now - hb_dernier) <= HB_TIMEOUT_MS; }

/* filtre 32 bits en liste exacte : deux ID étendus par banc, vers la FIFO0. Un filtre en liste a priorité sur le filtre 0
   (masque, tout accepté, FIFO1, traité par l'afficheur) : ces ID ne vont plus que dans la FIFO0. */
static void filtre_liste(uint16_t numero, uint32_t id_a, uint32_t id_b)
{
    can_filter_parameter_struct f;
    uint32_t a = (id_a << 3) | 4u, b = (id_b << 3) | 4u;   /* EXTID << 3, bit IDE */
    f.filter_number = numero;
    f.filter_mode = CAN_FILTERMODE_LIST;
    f.filter_bits = CAN_FILTERBITS_32BIT;
    f.filter_list_high = (uint16_t)(a >> 16); f.filter_list_low = (uint16_t)a;
    f.filter_mask_high = (uint16_t)(b >> 16); f.filter_mask_low = (uint16_t)b;
    f.filter_fifo_number = CAN_FIFO0;
    f.filter_enable = ENABLE;
    can_filter_init(&f);
}

void bench_can_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;     /* compteur de cycles pour le coût d'un envoi */
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    for (unsigned i = 0; i < NB_TRAMES; i++)            /* toutes dues tout de suite : E203 (hello) part au démarrage */
        trames[i].dernier = bench_ms - trames[i].periode_ms;
    filtre_liste(1, ID_ESTOP, ID_HEARTBEAT);
    filtre_liste(2, ID_CMD, ID_CMD);
}

static void repondre(uint8_t op, uint8_t seq, uint8_t statut)
{
    ack_op = op; ack_seq = seq; ack_statut = statut; ack_attente = 1;
}

static void commande(const can_receive_message_struct* m, uint32_t now)
{
    if (m->rx_ff != CAN_FF_EXTENDED) return;
    if (m->rx_efid == ID_ESTOP) {
        safety_fault(FAULT_BENCH_ESTOP);                 /* MOE = 0 d'abord, puis verrou */
        bench_ol_enable = 0; ol_par_can = 0; bench_walk = 0; bench_walk_ilim = 0;
        if (!defaut_banc) defaut_banc = FAULT_BENCH_ESTOP;
        repondre(0xFF, 0, 0);
        return;
    }
#ifdef ROUTE_BUILD
    (void)now;
    return;                                              /* build route : telemetrie en emission seule, seul l'ESTOP agit */
#endif
    if (m->rx_efid == ID_HEARTBEAT) {
        if (!hb_present(now)) hb_debut = now;            /* début d'une présence continue */
        hb_dernier = now; hb_vu = 1;
        if (ol_par_can && bench_ol_enable) bench_ol_keepalive = OL_KEEPALIVE_CAN;
        return;
    }
    if (m->rx_efid != ID_CMD || m->rx_dlen < 2) return;
    uint8_t op = m->rx_data[0], seq = m->rx_data[1];
    if (op == OP_OL_STOP) {
        bench_ol_enable = 0; bench_ol_ud = 0; ol_par_can = 0;
        repondre(op, seq, 0);
        return;
    }
    if (op == OP_PRECHARGE) {                            /* reglage seul, sans heartbeat ; refuse pendant un essai */
        if (m->rx_dlen < 3 || m->rx_data[2] > PRECHARGE_MAX_MS) { repondre(op, seq, 4); return; }
        if (ui_8_PWM_ON_Flag || bench_ol_enable || bench_ol_active || bench_walk) { repondre(op, seq, 7); return; }
        bench_precharge_ms = m->rx_data[2];
        repondre(op, seq, 0);
        return;
    }
    if (op == OP_WALK_STOP) {
        bench_walk = 0; bench_walk_ilim = 0;
        repondre(op, seq, 0);
        return;
    }
    if ((op != OP_OL_SET || m->rx_dlen < 5) && (op != OP_WALK_START || m->rx_dlen < 3)) { repondre(op, seq, 4); return; }
    if (safety_is_latched()) { repondre(op, seq, 2); return; }
    if (!offsets_valides) { repondre(op, seq, 5); return; }
    if (!hb_present(now) || (uint32_t)(now - hb_debut) < HB_ARMEMENT_MS) { repondre(op, seq, 1); return; }
    if (MS.brake_active_flag || (op == OP_OL_SET && MS.i_q_setpoint)) { repondre(op, seq, 3); return; }
    if (op == OP_WALK_START) {
        uint32_t h = (GPIO_ISTAT(GPIOC) >> 6) & 0x07u;
#ifdef BENCH_HALL_MESURE
        uint8_t angles = 1;
#else
        uint8_t angles = 0;
#endif
        if (!angles || h == 0u || h == 7u) { repondre(op, seq, 6); return; }
        if (bench_ol_enable || bench_ol_active) { repondre(op, seq, 7); return; }
        uint8_t il = m->rx_data[2];
        if (il > bench_walk_max) il = bench_walk_max;      /* plafonds à l'échelle de la variante (correctif F2) */
        if (il > bench_ph_max) il = bench_ph_max;
        bench_walk_ilim = il;
        bench_walk = 1;
        repondre(op, seq, 0);
        return;
    }
    if (bench_walk) { repondre(op, seq, 7); return; }
    uint16_t angle = (uint16_t)(m->rx_data[2] | (m->rx_data[3] << 8));
    uint8_t ud = m->rx_data[4] > BENCH_OL_UD_MAX ? BENCH_OL_UD_MAX : m->rx_data[4];
    bench_ol_angle = (q31_t)((uint32_t)angle << 16);    /* angle et u_d d'abord, activation ensuite */
    bench_ol_ud = ud;
    bench_ol_keepalive = OL_KEEPALIVE_CAN;
    ol_par_can = 1;
    bench_ol_enable = 1;
    repondre(op, seq, 0);
}

/* réception et surveillance : à chaque tour de boucle, avant l'émission */
static void recevoir(uint32_t now)
{
    if (CAN_RFIFO0(CAN0) & CAN_RFIFO0_RFO0) {            /* FIFO0 débordée : commandes perdues */
        if (rx_perdues < 255) rx_perdues++;
        CAN_RFIFO0(CAN0) = CAN_RFIFO0_RFO0;              /* effacement par écriture de 1 */
    }
    for (int n = 0; n < 3 && (CAN_RFIFO0(CAN0) & CAN_RFIFO0_RFL0); n++) {
        can_receive_message_struct m;
        can_message_receive(CAN0, CAN_FIFO0, &m);       /* libère l'entrée de la FIFO0 */
        commande(&m, now);
    }
    if (!bench_ol_enable && !bench_ol_active) ol_par_can = 0;   /* outil arrêté (keepalive, OL_STOP, défaut) */
    if (safety_is_latched()) { bench_walk = 0; bench_walk_ilim = 0; }
    if ((ol_par_can || bench_walk) && (uint32_t)(now - hb_dernier) > HB_TIMEOUT_MS) {
        safety_fault(FAULT_BENCH_HEARTBEAT);             /* MOE = 0 d'abord, puis verrou */
        bench_ol_enable = 0; ol_par_can = 0; bench_walk = 0; bench_walk_ilim = 0;
        if (!defaut_banc) defaut_banc = FAULT_BENCH_HEARTBEAT;
    }
}

void bench_can_sample(int16_t i1, int16_t i2, int16_t i3)
{
    int16_t v[3] = {i1, i2, i3};
    if (n_ech >= 1000) return;                           /* fenêtre non vidée : on n'accumule plus (pas de débordement) */
    for (int i = 0; i < 3; i++) {
        int32_t a = v[i] < 0 ? -v[i] : v[i];
        somme2[i] += (uint32_t)(a * a);
        if (a > crete) crete = a > 255 ? 255 : (uint8_t)a;
    }
    n_ech++;
}

static uint8_t remplir(const bench_trame_t* t, uint8_t* d)
{
    uint32_t gpio_hall = (GPIO_ISTAT(GPIOC) >> 6) & 0x07u;
    uint32_t err = CAN_ERR(CAN0);
    switch (((uint16_t)t->idx << 8) | t->sub) {
    case 0xE000:
        put16(d + 1, sat16(PI_iq.setpoint)); put16(d + 3, sat16(MS.i_q)); put16(d + 5, sat16(MS.i_d));
        d[7] = (uint8_t)(moe() | (ui_8_PWM_ON_Flag ? 2u : 0u) | (bench_walk ? 4u : 0u)
                         | (MS.brake_active_flag ? 8u : 0u) | (hb_present(bench_ms) ? 0x10u : 0u) | (safety_is_latched() ? 0x20u : 0u)
                         | (bench_ol_active ? 0x40u : 0u) | (offsets_ok() ? 0x80u : 0u));
        return 8;
    case 0xE001:
        put16(d + 1, sat16(MS.u_d)); put16(d + 3, sat16(MS.u_q)); put16(d + 5, (int32_t)usat16(MS.u_abs));
        d[7] = (uint8_t)(gpio_hall | ((ui8_hall_state & 0x07u) << 3));
        return 8;
    case 0xE002:
        d[1] = bench_ol_active; put16(d + 2, bench_ol_ud); put16(d + 4, (int32_t)((uint32_t)bench_ol_angle >> 16));
        put16(d + 6, bench_ol_keepalive);
        return 8;
    case 0xE100: {
        uint16_t n = n_ech ? n_ech : 1;
        for (int i = 0; i < 3; i++)                      /* efficace en 0,1 pt : sqrt(100 * somme / n) */
            put16(d + 1 + 2 * i, (int32_t)usat16((int32_t)isqrt32((uint32_t)(((uint64_t)somme2[i] * 100u) / n))));
        d[7] = crete;
        somme2[0] = somme2[1] = somme2[2] = 0; n_ech = 0; crete = 0;
        return 8;
    }
    case 0xE101:
        put16(d + 1, (int32_t)usat16(MS.Voltage / 10)); put16(d + 3, sat16(MS.Battery_Current / 10));
        put16(d + 5, voltage_raw_filtered); d[7] = MS.SOC;
        return 8;
    case 0xE102:
        put16(d + 1, (int32_t)usat16((int32_t)MS.Speedx100)); put16(d + 3, ui16_erps); d[5] = MS.cadence;
        put16(d + 6, (int32_t)usat16(MS.torque_on_crank));
        return 8;
    case 0xE103:
        put16(d + 1, MS.torque_filtered); d[3] = MS.assist_level; d[4] = bench_walk;
        put16(d + 5, sat16(MS.i_q_setpoint_temp)); d[7] = bench_walk_ilim;
        return 8;
    case 0xE200:
        d[1] = safety_fault_code_latched; d[2] = defaut_banc;
        d[3] = (uint8_t)(moe() | (ui_8_PWM_ON_Flag ? 2u : 0u) | (bench_ol_active ? 4u : 0u));
        d[4] = (uint8_t)gpio_hall; put16(d + 5, uint16_half_rotation_counter); d[7] = (uint8_t)(boot_reset_flags >> 24);
        return 8;
    case 0xE201:
        put16(d + 1, MS.int_Temperature); put16(d + 3, 0x7FFF); put16(d + 5, adc_value[6]); d[7] = 0xFF;
        return 8;
    case 0xE202:
        put16(d + 1, lost); d[3] = rx_perdues; d[4] = (uint8_t)(err >> 16); d[5] = (uint8_t)(err >> 24); put16(d + 6, cycles_max);
        return 8;
    case 0xE203:
        put16(d + 1, BENCH_CAN_VERSION); put32(d + 3, BENCH_GIT_HASH);
#ifdef BENCH_NO_EEPROM
        d[7] = 1u;
#else
        d[7] = 0u;
#endif
        d[7] |= (TEMP_SENSOR_REQUIRED ? 2u : 0u) | (CURRENT_SIGN < 0 ? 4u : 0u) | (BENCH_GIT_DIRTY ? 8u : 0u)
              | (variante_repli ? 0x10u : 0u);
        return 8;
    case 0xE204:
        d[1] = bench_ph_max; d[2] = (uint8_t)safety_ph_trip; d[3] = (uint8_t)MP.phase_current_max; d[4] = (uint8_t)(_U_MAX / 8);
        put16(d + 5, VOLTAGE_MIN); d[7] = BENCH_OL_UD_MAX;
        return 8;
    case 0xE205:
        put16(d + 1, offset_cal_ph[0]); put16(d + 3, offset_cal_ph[1]); put16(d + 5, offset_cal_ph[2]);
        {
            uint32_t h = (GPIO_ISTAT(GPIOC) >> 6) & 0x07u;
            d[7] = (uint8_t)(offsets_ok()
                             | ((ADC_IOFF0(ADC2) & 0xFFFu) == (uint32_t)offset_pont_ph[0] ? 2u : 0u)
                             | (offsets_valides ? 4u : 0u)
                             | (hall_stable_ticks >= 200u && MS.Speedx100 == 0 ? 8u : 0u)
                             | (h != 0u && h != 7u ? 0x10u : 0u));
        }
        return 8;
    case 0xE208:
        d[1] = cap_tx;
        for (int k = 0; k < 3; k++) { d[2 + k] = (uint8_t)cap_ph[k][cap_tx]; d[5 + k] = (uint8_t)cap_ph[k][cap_tx + 1]; }
        cap_tx = (uint8_t)((cap_tx + 2u) % CAP_N);
        return 8;
    case 0xE209:
        d[1] = cap_n; d[2] = cap_trip; d[3] = bench_precharge_ms; d[4] = cap_seq;
        put16(d + 5, (int32_t)(TIMER_CHCTL2(TIMER0) & 0xFFFFu)); d[7] = CAP_N;
        return 8;
    case 0xE20A: {
        uint8_t n = hj_n;
        if (n == 0) { d[1] = 0xFF; d[7] = hj_seq; return 8; }
        if (hj_tx >= n) hj_tx = 0;
        uint8_t k = (n < HJ_N) ? hj_tx : (uint8_t)((hj_idx + hj_tx) % HJ_N);   /* ordre chronologique */
        d[1] = hj_tx; d[2] = hj_case[k]; put16(d + 3, hj_tics[k]); d[5] = hj_fl[k]; d[6] = hj_ang[k]; d[7] = hj_seq;
        hj_tx++;
        return 8;
    }
    case 0xE20B: {
        uint8_t n = pt_n;
        d[6] = pt_seq; d[7] = (uint8_t)((pt_gel ? 1u : 0u) | (n >= PT_N ? 2u : 0u));
        if (n == 0) { d[1] = 0xFF; return 8; }
        if (pt_tx >= n) pt_tx = 0;
        uint8_t k = (n < PT_N) ? pt_tx : (uint8_t)((pt_idx + pt_tx) % PT_N);   /* ordre chronologique */
        d[1] = pt_tx; d[2] = (uint8_t)pt_ph[0][k]; d[3] = (uint8_t)pt_ph[1][k]; d[4] = (uint8_t)pt_ph[2][k]; d[5] = pt_st[k];
        pt_tx++;
        return 8;
    }
    case 0xE20C: {
        uint8_t n = pt_n;
        d[6] = pt_seq; d[7] = (uint8_t)((pt_gel ? 1u : 0u) | (n >= PT_N ? 2u : 0u));
        if (n == 0) { d[1] = 0xFF; return 8; }
        if (pc_tx >= n) pc_tx = 0;
        uint8_t k = (n < PT_N) ? pc_tx : (uint8_t)((pt_idx + pc_tx) % PT_N);   /* ordre chronologique */
        d[1] = pc_tx; d[2] = (uint8_t)pt_ud[k]; d[3] = (uint8_t)pt_uq[k]; d[4] = pt_ang[k]; d[5] = pt_x[k];
        pc_tx++;
        return 8;
    }
    case 0xE20E:
        d[1] = (uint8_t)MP.phase_current_max; put16(d + 2, safety_ph_trip); d[4] = (uint8_t)MP.reverse;
        d[5] = (uint8_t)(MP.speedLimitx100 / 100);
#ifdef ROUTE_BUILD
        d[6] = ROUTE_NIVEAU_MAX;
#else
        d[6] = 0u;
#endif
        d[7] = MS.assist_level;
        return 8;
    case 0xE20D: {
        uint8_t n = pt_n;
        d[6] = pt_seq; d[7] = (uint8_t)((pt_gel ? 1u : 0u) | (n >= PT_N ? 2u : 0u));
        if (n == 0) { d[1] = 0xFF; return 8; }
        if (pa_tx >= n) pa_tx = 0;
        uint8_t k = (n < PT_N) ? pa_tx : (uint8_t)((pt_idx + pa_tx) % PT_N);   /* ordre chronologique */
        d[1] = pa_tx; d[2] = (uint8_t)pt_av[0][k]; d[3] = (uint8_t)pt_av[1][k]; d[4] = (uint8_t)pt_av[2][k]; d[5] = 0;
        pa_tx++;
        return 8;
    }
#ifdef ROUTE_BUILD
    case 0xE20F:
        d[1] = pedalier_diag.niveaux; put16(d + 2, pedalier_diag.fronts_pc12); put16(d + 4, pedalier_diag.fronts_pd2);
        put16(d + 6, pedalier_exti12);
        return 8;
    case 0xE210: {
        uint8_t n = pd2_journal.n;
        d[6] = n; d[7] = pd2_journal.seq;
        if (n == 0) { d[1] = 0xFF; return 8; }
        if (pj_tx >= n) pj_tx = 0;
        uint8_t k = (n < PD2_JN) ? pj_tx : (uint8_t)((pd2_journal.idx + pj_tx) % PD2_JN);   /* ordre chronologique */
        d[1] = pj_tx; put16(d + 2, pd2_journal.periode[k]); put16(d + 4, pd2_journal.haut[k]);
        pj_tx++;
        return 8;
    }
    case 0xE212:
        put16(d + 1, freq_diag.isr_hz); put16(d + 3, freq_diag.trait_hz); put16(d + 5, freq_diag.boucle_hz);
        d[7] = freq_diag.pwm_q4;
        return 8;
    case 0xE211:
        put16(d + 1, couple_zero.zero); put16(d + 3, couple_zero.dernier); d[5] = couple_zero.n; d[6] = 0; d[7] = 0;
        return 8;
#endif
    case 0xE207:
        d[1] = bench_variante;
        put16(d + 2, offset_pont_ph[0]); put16(d + 4, offset_pont_ph[1]); put16(d + 6, offset_pont_ph[2]);
        return 8;
    default:
        return 0;
    }
}

static uint8_t boites_libres(void)
{
    uint32_t ts = CAN_TSTAT(CAN0);
    return (uint8_t)(((ts & CAN_TSTAT_TME0) ? 1 : 0) + ((ts & CAN_TSTAT_TME1) ? 1 : 0) + ((ts & CAN_TSTAT_TME2) ? 1 : 0));
}

static void envoyer(uint32_t id, const uint8_t* d, uint8_t dlen)
{
    can_trasnmit_message_struct m;
    m.tx_sfid = 0; m.tx_efid = id; m.tx_ff = CAN_FF_EXTENDED; m.tx_ft = CAN_FT_DATA; m.tx_dlen = dlen;
    for (int i = 0; i < 8; i++) m.tx_data[i] = d[i];
    can_message_transmit(CAN0, &m);
}

void bench_can_poll(void)
{
    uint32_t now = bench_ms;
    recevoir(now);
    if (ack_attente && boites_libres() >= 2) {          /* réponse à une commande : avant la télémétrie */
        uint8_t d[8] = {ack_tx_seq++, ack_op, ack_seq, ack_statut, 0, 0, 0, 0};
        envoyer(BENCH_ID(0xE2, 0x06), d, 4);
        ack_attente = 0;
        return;
    }
    for (unsigned k = 0; k < NB_TRAMES; k++) {           /* tourniquet : une trame due au plus par appel */
        bench_trame_t* t = &trames[prochaine];
        prochaine = (uint8_t)((prochaine + 1u) % NB_TRAMES);
        if ((uint32_t)(now - t->dernier) < t->periode_ms) continue;
        if (boites_libres() < 2) {                                /* jamais d'attente : une boîte reste aux trames de l'afficheur */
            /* trame gardée due et réessayée aux tours suivants ; perdue seulement après une période entière de retard
               (26/10 : la marquer envoyée faisait sauter E201-E205 à chaque fois, dues en même temps que E200) */
            if ((uint32_t)(now - t->dernier) >= 2u * t->periode_ms) { t->dernier = now; lost++; }
            return;
        }
        t->dernier = now;
        uint32_t c0 = DWT->CYCCNT;
        uint8_t d[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        d[0] = t->seq++;
        uint8_t dlen = remplir(t, d);
        if (dlen) envoyer(BENCH_ID(t->idx, t->sub), d, dlen);
        uint32_t c = DWT->CYCCNT - c0;
        if (c > cycles_max) cycles_max = c > 65535u ? 65535u : (uint16_t)c;
        return;
    }
}
#endif /* BENCH_BUILD */
