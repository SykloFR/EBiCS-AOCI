/*
 * ramtest_tim.c : essai en RAM des bases de temps de timer1_config() / timer2_config() (src/main.c).
 * Memes formules que main.c ; mesure sur 100 ms (compteur de cycles DWT). Aucune sortie, aucune broche.
 *   TIMER1 (TIM2) : 4 kHz attendu ; TIMER2 (TIM3) : 500 kHz attendu.
 */
#include "gd32f10x.h"
#define RES_ADDR 0x2000B000U
typedef struct { uint32_t magic, sysclk, psc1, per1, psc2, updates1, cnt2, cycles; } result_t;
#define R ((volatile result_t *)RES_ADDR)

__attribute__((section(".entry"), noreturn))
void test_start(void)
{
    timer_parameter_struct tp;
    __disable_irq();
    R->magic = 0;
    SystemInit();
    R->sysclk = SystemCoreClock;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    rcu_periph_clock_enable(RCU_TIMER1);
    rcu_periph_clock_enable(RCU_TIMER2);
    timer_deinit(TIMER1);
    timer_deinit(TIMER2);

    /* TIMER1 : memes formules que timer1_config() */
    tp.prescaler = 1;
    tp.alignedmode = TIMER_COUNTER_EDGE;
    tp.counterdirection = TIMER_COUNTER_UP;
    tp.period = (SystemCoreClock / 2U) / 4000U - 1U;
    tp.clockdivision = TIMER_CKDIV_DIV1;
    tp.repetitioncounter = 0;
    timer_init(TIMER1, &tp);
    R->psc1 = tp.prescaler; R->per1 = tp.period;

    /* TIMER2 : memes formules que timer2_config() */
    tp.prescaler = (uint16_t)(SystemCoreClock / 500000U - 1U);
    tp.period = 0xFFFF;
    timer_init(TIMER2, &tp);
    R->psc2 = tp.prescaler;

    timer_flag_clear(TIMER1, TIMER_FLAG_UP);
    timer_enable(TIMER1);
    timer_enable(TIMER2);

    uint32_t t0 = DWT->CYCCNT, n = 0;
    uint16_t c0 = (uint16_t)timer_counter_read(TIMER2);
    uint32_t span = SystemCoreClock / 10U;           /* 100 ms */
    while ((DWT->CYCCNT - t0) < span) {
        if (timer_flag_get(TIMER1, TIMER_FLAG_UP) == SET) {
            timer_flag_clear(TIMER1, TIMER_FLAG_UP);
            n++;
        }
    }
    R->cycles = DWT->CYCCNT - t0;
    R->cnt2 = (uint16_t)(timer_counter_read(TIMER2) - c0);
    R->updates1 = n;
    timer_disable(TIMER1);
    timer_disable(TIMER2);
    R->magic = 0xA0C1A0C1U;
    __asm volatile ("bkpt #0");
    while (1) { }
}
