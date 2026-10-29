/*
 * Zero de couple adaptatif (build route, 27/10/2026), comme le firmware d'origine de l'AOCI (analyse-commande-moteur.md
 * 6.1) : une valeur par impulsion du pedalier dans un tampon des CZ_N dernieres (fenetre de validite 300 a 4900 mV),
 * zero = moyenne des 2e et 3e plus petites. 750 mV (seuil fixe d'EBiCS) tant que CZ_N valeurs ne sont pas reunies.
 * CZ_N : 48 en route (un tour, 29/10), 20 ailleurs (origine).
 * Fonction pure, testee par tools/test_couple_zero.c et tools/test_chaine_couple.c.
 */
#ifndef COUPLE_ZERO_H_
#define COUPLE_ZERO_H_

#include <stdint.h>

#ifdef ROUTE_BUILD
#define CZ_N        48u      /* un tour de pedalier a 48 impulsions (29/10, correctif-chaine-couple-attendu-20261029.md) :
                                20 = 150 deg, moins qu'une poussee ; le zero montait avec l'effort (B1.3 : 803 mV en
                                moyenne, jusqu'a 992) */
#else
#define CZ_N        20u
#endif
#define CZ_INIT     750u     /* seuil fixe d'EBiCS, garde pendant l'amorce */
#define CZ_MIN_MV   300
#define CZ_MAX_MV   4900

typedef struct {
    uint16_t ech[CZ_N];
    uint8_t  idx;            /* prochaine case ecrite */
    uint8_t  n;              /* valeurs reunies (<= CZ_N) */
    uint16_t zero;           /* zero courant (mV) */
    uint16_t dernier;        /* derniere valeur entree (mV), telemetrie */
} couple_zero_t;

#define COUPLE_ZERO_INIT {{0}, 0u, 0u, CZ_INIT, 0u}

/* entre la valeur de couple de l'impulsion (mV) et renvoie le zero a appliquer */
static inline uint16_t couple_zero_ajoute(couple_zero_t *c, int32_t mv)
{
    if (mv < CZ_MIN_MV || mv > CZ_MAX_MV) return c->zero;    /* hors fenetre : ignoree */
    c->ech[c->idx] = (uint16_t)mv;
    c->dernier = (uint16_t)mv;
    c->idx = (uint8_t)((c->idx + 1u) % CZ_N);
    if (c->n < CZ_N) c->n++;
    if (c->n < CZ_N) return c->zero;                         /* amorce : CZ_INIT */
    uint16_t m0 = 0xFFFFu, m1 = 0xFFFFu, m2 = 0xFFFFu;       /* trois plus petites, m0 <= m1 <= m2 */
    for (uint8_t i = 0; i < CZ_N; i++) {
        uint16_t v = c->ech[i];
        if (v < m0) { m2 = m1; m1 = m0; m0 = v; }
        else if (v < m1) { m2 = m1; m1 = v; }
        else if (v < m2) { m2 = v; }
    }
    c->zero = (uint16_t)((m1 + m2) / 2u);
    return c->zero;
}

#endif /* COUPLE_ZERO_H_ */
