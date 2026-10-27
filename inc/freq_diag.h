/*
 * Frequence reelle de reg_ADC_processing (build route, diagnostic du 27/10/2026, trame E212) : compteurs libres 32 bits
 * (debordements de TIMER1, appels de reg_ADC_processing, tours de la boucle principale), figes toutes les 1000 ms de
 * SysTick en ecarts par seconde, avec la part de la seconde PWM actif. Fonction pure, testee par tools/test_freq_diag.c.
 */
#ifndef FREQ_DIAG_H_
#define FREQ_DIAG_H_

#include <stdint.h>

typedef struct {
    uint32_t prec_isr, prec_trait, prec_boucle;   /* compteurs au debut de la fenetre */
    uint16_t ms, ms_pwm;                          /* millisecondes de la fenetre, dont PWM actif */
    uint16_t isr_hz, trait_hz, boucle_hz;         /* dernieres valeurs par seconde (saturees a 65535) */
    uint8_t  pwm_q4;                              /* ms PWM actif / 4 sur la derniere fenetre (0..250) */
    uint8_t  fenetres;                            /* fenetres closes (tourne sur 8 bits) */
} freq_diag_t;

static inline uint16_t freq_sat16(uint32_t v) { return v > 0xFFFFu ? 0xFFFFu : (uint16_t)v; }

/* a appeler a chaque milliseconde (SysTick) avec les compteurs libres et l'etat du PWM */
static inline void freq_diag_ms(freq_diag_t *f, uint32_t n_isr, uint32_t n_trait, uint32_t n_boucle, uint8_t pwm)
{
    if (pwm) f->ms_pwm++;
    if (++f->ms < 1000u) return;
    f->isr_hz    = freq_sat16(n_isr - f->prec_isr);       /* soustraction modulo 2^32 : passage par 0 sans erreur */
    f->trait_hz  = freq_sat16(n_trait - f->prec_trait);
    f->boucle_hz = freq_sat16(n_boucle - f->prec_boucle);
    f->pwm_q4    = (uint8_t)(f->ms_pwm / 4u);
    f->prec_isr = n_isr; f->prec_trait = n_trait; f->prec_boucle = n_boucle;
    f->ms = 0u; f->ms_pwm = 0u;
    f->fenetres++;
}

#endif /* FREQ_DIAG_H_ */
