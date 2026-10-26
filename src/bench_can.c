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
 *   E103 20 Hz  couple filtré (u16), niveau (u8), walk (u8), consigne amont i_q_setpoint_temp (i16, pts), ilim (u8, pts)
 *   E200 10 Hz  défaut (u8), défaut banc (u8, 0 en T1), état (b0 MOE, b1 PWM_ON, b2 outil), Hall brut (u8),
 *               half_rotation_counter (u16), cause du reset (u8, RCU_RSTSCK >> 24)
 *   E201 10 Hz  T contrôleur (i16, °C), T moteur (i16, 0x7FFF = non mesurée : PB0 non lue), ADC PB1 (u16), pile (u8, 0xFF = non mesurée)
 *   E202 10 Hz  trames perdues (u16), réceptions perdues (u8, 0 en T1), TEC (u8), REC (u8), cycles max d'un envoi (u16)
 *   E203  1 Hz  + au démarrage (hello) : version (u16), hash git court (u32), macros banc (u8 : b0 BENCH_NO_EEPROM,
 *               b1 TEMP_SENSOR_REQUIRED, b2 CURRENT_SIGN < 0, b3 arbre git modifié)
 *   E204  1 Hz  PH_CURRENT_MAX, PH_CURRENT_TRIP, ilim (u8, pts), _U_MAX / 8 (u8), VOLTAGE_MIN (u16, pt), BENCH_OL_UD_MAX (u8)
 *   E205  1 Hz  offsets bras bas fermé ph1, ph2, ph3 (u16, pt), état (b0 dans la plage, b1 appliqués)
 */
#include "bench_can.h"

#ifdef BENCH_BUILD
#include "main.h"
#include "config.h"
#include "FOC.h"
#include "safety.h"

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
extern volatile uint8_t bench_ol_active;
extern volatile int16_t bench_ol_ud;
extern volatile q31_t bench_ol_angle;
extern volatile uint16_t bench_ol_keepalive;

volatile uint32_t bench_ms = 0;

typedef struct { uint8_t idx, sub; uint16_t periode_ms; uint32_t dernier; uint8_t seq; } bench_trame_t;

static bench_trame_t trames[] = {
    {0xE0, 0x00,   20, 0, 0}, {0xE0, 0x01,   20, 0, 0}, {0xE0, 0x02,   50, 0, 0},
    {0xE1, 0x00,   50, 0, 0}, {0xE1, 0x01,   50, 0, 0}, {0xE1, 0x02,   50, 0, 0}, {0xE1, 0x03, 50, 0, 0},
    {0xE2, 0x00,  100, 0, 0}, {0xE2, 0x01,  100, 0, 0}, {0xE2, 0x02,  100, 0, 0},
    {0xE2, 0x03, 1000, 0, 0}, {0xE2, 0x04, 1000, 0, 0}, {0xE2, 0x05, 1000, 0, 0},
};
#define NB_TRAMES (sizeof(trames) / sizeof(trames[0]))

static uint16_t lost = 0, cycles_max = 0;
static uint8_t prochaine = 0;
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

void bench_can_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;     /* compteur de cycles pour le coût d'un envoi */
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    for (unsigned i = 0; i < NB_TRAMES; i++)            /* toutes dues tout de suite : E203 (hello) part au démarrage */
        trames[i].dernier = bench_ms - trames[i].periode_ms;
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
        d[7] = (uint8_t)(moe() | (ui_8_PWM_ON_Flag ? 2u : 0u) | (MS.pushassist_flag ? 4u : 0u)
                         | (MS.brake_active_flag ? 8u : 0u) | (safety_is_latched() ? 0x20u : 0u)
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
        put16(d + 1, MS.torque_filtered); d[3] = MS.assist_level; d[4] = MS.pushassist_flag ? 1u : 0u;
        put16(d + 5, sat16(MS.i_q_setpoint_temp)); d[7] = (uint8_t)MP.phase_current_max;
        return 8;
    case 0xE200:
        d[1] = safety_fault_code_latched; d[2] = 0;
        d[3] = (uint8_t)(moe() | (ui_8_PWM_ON_Flag ? 2u : 0u) | (bench_ol_active ? 4u : 0u));
        d[4] = (uint8_t)gpio_hall; put16(d + 5, uint16_half_rotation_counter); d[7] = (uint8_t)(boot_reset_flags >> 24);
        return 8;
    case 0xE201:
        put16(d + 1, MS.int_Temperature); put16(d + 3, 0x7FFF); put16(d + 5, adc_value[6]); d[7] = 0xFF;
        return 8;
    case 0xE202:
        put16(d + 1, lost); d[3] = 0; d[4] = (uint8_t)(err >> 16); d[5] = (uint8_t)(err >> 24); put16(d + 6, cycles_max);
        return 8;
    case 0xE203:
        put16(d + 1, BENCH_CAN_VERSION); put32(d + 3, BENCH_GIT_HASH);
#ifdef BENCH_NO_EEPROM
        d[7] = 1u;
#else
        d[7] = 0u;
#endif
        d[7] |= (TEMP_SENSOR_REQUIRED ? 2u : 0u) | (CURRENT_SIGN < 0 ? 4u : 0u) | (BENCH_GIT_DIRTY ? 8u : 0u);
        return 8;
    case 0xE204:
        d[1] = PH_CURRENT_MAX; d[2] = PH_CURRENT_TRIP; d[3] = (uint8_t)MP.phase_current_max; d[4] = (uint8_t)(_U_MAX / 8);
        put16(d + 5, VOLTAGE_MIN); d[7] = BENCH_OL_UD_MAX;
        return 8;
    case 0xE205:
        put16(d + 1, offset_cal_ph[0]); put16(d + 3, offset_cal_ph[1]); put16(d + 5, offset_cal_ph[2]);
        d[7] = (uint8_t)(offsets_ok()
                         | ((ADC_IOFF0(ADC2) & 0xFFFu) == (uint32_t)offset_cal_ph[0] ? 2u : 0u));
        return 8;
    default:
        return 0;
    }
}

void bench_can_poll(void)
{
    uint32_t now = bench_ms;
    for (unsigned k = 0; k < NB_TRAMES; k++) {           /* tourniquet : une trame due au plus par appel */
        bench_trame_t* t = &trames[prochaine];
        prochaine = (uint8_t)((prochaine + 1u) % NB_TRAMES);
        if ((uint32_t)(now - t->dernier) < t->periode_ms) continue;
        t->dernier = now;
        uint32_t ts = CAN_TSTAT(CAN0);
        uint8_t libres = (uint8_t)(((ts & CAN_TSTAT_TME0) ? 1 : 0) + ((ts & CAN_TSTAT_TME1) ? 1 : 0) + ((ts & CAN_TSTAT_TME2) ? 1 : 0));
        if (libres < 2) { lost++; return; }              /* jamais d'attente : la boîte reste aux trames de l'afficheur */
        uint32_t c0 = DWT->CYCCNT;
        can_trasnmit_message_struct m;
        m.tx_sfid = 0; m.tx_efid = BENCH_ID(t->idx, t->sub); m.tx_ff = CAN_FF_EXTENDED; m.tx_ft = CAN_FT_DATA;
        for (int i = 0; i < 8; i++) m.tx_data[i] = 0;
        m.tx_data[0] = t->seq++;
        m.tx_dlen = remplir(t, m.tx_data);
        if (m.tx_dlen) can_message_transmit(CAN0, &m);
        uint32_t c = DWT->CYCCNT - c0;
        if (c > cycles_max) cycles_max = c > 65535u ? 65535u : (uint16_t)c;
        return;
    }
}
#endif /* BENCH_BUILD */
