/*
 * ramtest_can.c : essai en RAM du CAN en boucle interne silencieuse (SILENT + LOOPBACK).
 * Les broches CAN ne sont PAS pilotees (mode silencieux : TX reste recessif), rien n'est ecrit en flash.
 *
 * Verifie : parametres de debit de can_networking_init() (copie fidele), formatage des ID AOCI
 * (macros de CAN_Display.h), filtre "tout accepter" en FIFO1, et mesure la duree reelle d'une trame
 * (compteur de cycles DWT) pour en deduire le debit.
 */
#include "gd32f10x.h"
#include "CAN_Display.h"

#define RES_ADDR 0x2000B000U
typedef struct {
    uint32_t magic, step, sysclk;
    uint32_t t_rx[2];                 /* cycles DWT a la reception de chaque trame */
    uint32_t id_rx[2];
    uint8_t  data_rx[2][8];
    uint32_t len_rx[2];
    uint32_t ff_rx[2];
    uint32_t err_flags;
    uint32_t prescaler, bs1, bs2;
} result_t;
#define R ((volatile result_t *)RES_ADDR)

static void spin(volatile uint32_t n) { while (n--) { } }

__attribute__((section(".entry"), noreturn))
void test_start(void)
{
    can_parameter_struct p;
    can_filter_parameter_struct f;
    can_trasnmit_message_struct tx[2];
    can_receive_message_struct rx;

    __disable_irq();
    R->magic = 0; R->step = 1;
    SystemInit();
    R->sysclk = SystemCoreClock;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    rcu_periph_clock_enable(RCU_CAN0);
    can_struct_para_init(CAN_INIT_STRUCT, &p);
    can_struct_para_init(CAN_FILTER_STRUCT, &f);
    can_deinit(CAN0);

    /* --- copie de can_networking_init() (src/main.c), sauf le mode --- */
    p.time_triggered = DISABLE;
    p.auto_bus_off_recovery = ENABLE;
    p.auto_wake_up = DISABLE;
    p.auto_retrans = ENABLE;
    p.rec_fifo_overwrite = DISABLE;
    p.trans_fifo_order = DISABLE;
    p.working_mode = CAN_SILENT_LOOPBACK_MODE;      /* seule difference : rien sur le bus */
    p.resync_jump_width = CAN_BT_SJW_1TQ;
    if (SystemCoreClock == 66000000U) {
        p.time_segment_1 = CAN_BT_BS1_9TQ;  p.time_segment_2 = CAN_BT_BS2_2TQ;  p.prescaler = 11;
    } else {
        p.time_segment_1 = CAN_BT_BS1_13TQ; p.time_segment_2 = CAN_BT_BS2_2TQ;  p.prescaler = 8;
    }
    R->prescaler = p.prescaler;
    can_init(CAN0, &p);

    f.filter_number = 0;
    f.filter_mode = CAN_FILTERMODE_MASK;
    f.filter_bits = CAN_FILTERBITS_32BIT;
    f.filter_list_high = 0; f.filter_list_low = 0; f.filter_mask_high = 0; f.filter_mask_low = 0;
    f.filter_fifo_number = CAN_FIFO1;
    f.filter_enable = ENABLE;
    can_filter_init(&f);
    R->step = 2;

    for (int i = 0; i < 2; i++) {
        tx[i].tx_sfid = 0;
        tx[i].tx_efid = CAN_EFID_BCAST(0x3200U + (uint32_t)i);     /* 0x12F83200 puis 0x12F83201 */
        tx[i].tx_ff = CAN_FF_EXTENDED;
        tx[i].tx_ft = CAN_FT_DATA;
        tx[i].tx_dlen = 8;
        for (int k = 0; k < 8; k++) tx[i].tx_data[k] = (uint8_t)(0x10 * (i + 1) + k * 3 + 0x07);
    }
    spin(300000);                                   /* laisse le controleur se synchroniser sur le bus (11 bits recessifs) */
    can_message_transmit(CAN0, &tx[0]);
    can_message_transmit(CAN0, &tx[1]);
    R->step = 3;

    for (int n = 0; n < 2; n++) {
        uint32_t guard = 40000000;
        while (can_receive_message_length_get(CAN0, CAN_FIFO1) == 0 && --guard) { }
        R->t_rx[n] = DWT->CYCCNT;
        if (!guard) break;
        can_message_receive(CAN0, CAN_FIFO1, &rx);
        R->id_rx[n] = rx.rx_efid;
        R->len_rx[n] = rx.rx_dlen;
        R->ff_rx[n] = rx.rx_ff;
        for (int k = 0; k < 8; k++) R->data_rx[n][k] = rx.rx_data[k];
    }
    R->err_flags = CAN_ERR(CAN0);
    R->step = 4;
    R->magic = 0xA0C1A0C1U;
    __asm volatile ("bkpt #0");
    while (1) { }
}
