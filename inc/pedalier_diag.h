/*
 * Diagnostic du pedalier (build route, 27/10/2026) : EBiCS attend les impulsions sur PC12 (EXTI12, sens sur PD2, carte M820),
 * le firmware d'origine de l'AOCI mesure la cadence sur PD2. On compte les fronts des deux broches, echantillonnees a
 * 4 kHz sans anti-rebond, pour savoir laquelle porte les impulsions (trame E20F). Aucune action sur la commande.
 * Fonction pure, testee par tools/test_pedalier_diag.c.
 */
#ifndef PEDALIER_DIAG_H_
#define PEDALIER_DIAG_H_

#include <stdint.h>

typedef struct {
    uint8_t  niveaux;        /* b0 PC12, b1 PD2 (dernier echantillon) */
    uint8_t  amorce;         /* 0 tant qu'aucun echantillon : le premier ne compte pas de front */
    uint16_t fronts_pc12;    /* fronts montants et descendants, sur 16 bits (retour a 0 apres 65535) */
    uint16_t fronts_pd2;
} pedalier_diag_t;

static inline void pedalier_echantillon(pedalier_diag_t *s, uint8_t pc12, uint8_t pd2)
{
    uint8_t n = (uint8_t)((pc12 ? 1u : 0u) | (pd2 ? 2u : 0u));
    if (s->amorce) {
        uint8_t x = (uint8_t)(n ^ s->niveaux);
        if (x & 1u) s->fronts_pc12++;
        if (x & 2u) s->fronts_pd2++;
    }
    s->niveaux = n;
    s->amorce = 1u;
}

#endif /* PEDALIER_DIAG_H_ */
