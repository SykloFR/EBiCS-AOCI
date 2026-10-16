/*
 * ramtest.c : essai en RAM, SANS ECRITURE FLASH, SANS TOUCHER AUX SORTIES.
 *
 * Execute SystemInit() du portage (port_f10x/system_aoci.c) sur la vraie carte, puis :
 *   - releve les registres d'horloge ;
 *   - mesure la frequence reelle du coeur avec le compteur de cycles DWT (la duree mur est mesuree par l'hote) ;
 *   - lit une fois chaque voie ADC 0..17 (declenchement logiciel, 239,5 cycles) ;
 *   - releve les entrees GPIO (IDR) ;
 *   - ne configure AUCUNE broche et n'ecrit dans AUCUN registre GPIO / TIM1.
 * Les interruptions sont masquees des le debut. L'hote relance ensuite la carte par un reset.
 */
#include "gd32f10x.h"

#define RES_ADDR 0x2000B000U

typedef struct {
    uint32_t magic;          /* 0xA0C1A0C1 quand l'essai est termine */
    uint32_t step;           /* etape atteinte */
    uint32_t sysclk;         /* SystemCoreClock apres SystemInit */
    uint32_t rcu_ctl, rcu_cfg0;
    uint32_t cycles_start, cycles_end;
    uint32_t dwt_ok;
    uint16_t adc[18];
    uint32_t adc_ok;
    uint32_t idr[4];         /* GPIOA..D IDR */
    uint32_t adc_cal_ok;
    uint32_t acr;            /* FLASH_ACR / FMC_WS */
} result_t;

#define R ((volatile result_t *)RES_ADDR)

static void spin(volatile uint32_t n) { while (n--) { } }

__attribute__((section(".entry"), noreturn))
void test_start(void)
{
    __disable_irq();
    R->magic = 0;
    R->step = 1;

    SystemInit();
    R->step = 2;
    R->sysclk = SystemCoreClock;
    R->acr = FMC_WS;
    R->rcu_ctl = RCU_CTL;
    R->rcu_cfg0 = RCU_CFG0;

    /* compteur de cycles DWT : debut de la mesure de frequence */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    spin(10);
    R->dwt_ok = (DWT->CYCCNT != 0) ? 1U : 0U;
    R->step = 3;

    /* ADC : lecture logicielle de toutes les voies, horloge ADC = APB2 / 6 (11 MHz) */
    rcu_periph_clock_enable(RCU_ADC0);
    rcu_adc_clock_config(RCU_CKADC_CKAPB2_DIV6);
    adc_deinit(ADC0);
    adc_mode_config(ADC_MODE_FREE);
    adc_special_function_config(ADC0, ADC_SCAN_MODE, DISABLE);
    adc_special_function_config(ADC0, ADC_CONTINUOUS_MODE, DISABLE);
    adc_data_alignment_config(ADC0, ADC_DATAALIGN_RIGHT);
    adc_channel_length_config(ADC0, ADC_REGULAR_CHANNEL, 1);
    adc_external_trigger_source_config(ADC0, ADC_REGULAR_CHANNEL, ADC0_1_2_EXTTRIG_REGULAR_NONE);
    adc_external_trigger_config(ADC0, ADC_REGULAR_CHANNEL, ENABLE);
    adc_tempsensor_vrefint_enable();
    adc_enable(ADC0);
    spin(200000);
    adc_calibration_enable(ADC0);
    R->adc_cal_ok = 1;
    R->step = 4;

    for (uint8_t ch = 0; ch < 18; ch++) {
        adc_regular_channel_config(ADC0, 0, ch, ADC_SAMPLETIME_239POINT5);
        adc_flag_clear(ADC0, ADC_FLAG_EOC);
        adc_software_trigger_enable(ADC0, ADC_REGULAR_CHANNEL);
        uint32_t guard = 2000000;
        while (RESET == adc_flag_get(ADC0, ADC_FLAG_EOC) && --guard) { }
        R->adc[ch] = guard ? adc_regular_data_read(ADC0) : 0xFFFFU;
    }
    R->adc_ok = 1;
    R->step = 5;

    R->idr[0] = GPIO_ISTAT(GPIOA);
    R->idr[1] = GPIO_ISTAT(GPIOB);
    R->idr[2] = GPIO_ISTAT(GPIOC);
    R->idr[3] = GPIO_ISTAT(GPIOD);

    /* mesure de frequence : attend ~2 s a 66 MHz (132 M cycles), puis rend la main a l'hote */
    R->cycles_start = DWT->CYCCNT;
    R->magic = 0xA0C1A0C0U;                 /* signal "mesure en cours" */
    while ((DWT->CYCCNT - R->cycles_start) < 132000000U) { }
    R->cycles_end = DWT->CYCCNT;
    R->step = 6;
    R->magic = 0xA0C1A0C1U;
    __asm volatile ("bkpt #0");
    while (1) { }
}
