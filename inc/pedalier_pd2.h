/*
 * Cadence du pedalier sur PD2 (build route, 27/10/2026, diagnostic E20F) : voie unique, 48 impulsions par tour de
 * manivelle, PC12 / EXTI12 muets. PD2 n'a pas d'interruption (ligne EXTI2 prise par le capteur de vitesse PB2) : elle est
 * echantillonnee a 4 kHz avec l'anti-rebond du firmware d'origine (2 ms = 8 echantillons). Fonctions pures, testees par
 * tools/test_pedalier_pd2.c.
 */
#ifndef PEDALIER_PD2_H_
#define PEDALIER_PD2_H_

#include <stdint.h>

#define PD2_ANTIREBOND_ECH  8u      /* 2 ms a 4 kHz : niveau pris apres 8 echantillons identiques consecutifs */
#define PD2_IMPULSIONS_TOUR 48u     /* mesure du 27/10 (95 fronts sur un tour precis) */
#define PD2_CADENCE_NUM     (4000u * 60u / PD2_IMPULSIONS_TOUR)   /* tr/min = 5000 / periode en ticks de 4 kHz */
#define PD2_PERIODE_MIN     20u     /* 5 ms : 250 tr/min (70 ticks d'EBiCS plafonnaient a 71 tr/min avec 48 impulsions) */

typedef struct {
    uint8_t stable;          /* niveau valide */
    uint8_t compte;          /* echantillons consecutifs differents du niveau valide */
    uint8_t amorce;          /* 0 avant le premier echantillon */
} pd2_antirebond_t;

/* renvoie 1 sur un front descendant valide (niveau stable bas apres un niveau stable haut), 0 sinon */
static inline uint8_t pd2_echantillon(pd2_antirebond_t *s, uint8_t brut)
{
    uint8_t n = brut ? 1u : 0u;
    if (!s->amorce) { s->stable = n; s->compte = 0u; s->amorce = 1u; return 0u; }
    if (n == s->stable) { s->compte = 0u; return 0u; }
    if (++s->compte < PD2_ANTIREBOND_ECH) return 0u;
    s->stable = n; s->compte = 0u;
    return n == 0u ? 1u : 0u;
}

/* Journal des PD2_JN dernieres impulsions validees (diagnostic du 27/10, trame E210) : a chaque front descendant valide,
   periode depuis le front descendant precedent et duree du niveau valide haut qui le precede (ticks de 4 kHz, satures).
   Tampon circulaire ecrase en continu ; aucune action sur la commande. */
#define PD2_JN 64u

typedef struct {
    uint16_t periode[PD2_JN];
    uint16_t haut[PD2_JN];
    uint8_t  idx;            /* prochaine case ecrite */
    uint8_t  n;              /* entrees valides (<= PD2_JN) */
    uint8_t  seq;            /* numero d'impulsion (tourne sur 8 bits) */
    uint8_t  prec;           /* niveau valide de l'echantillon precedent */
    uint16_t t_periode;      /* ticks depuis le dernier front descendant valide */
    uint16_t t_haut;         /* ticks au niveau valide haut depuis le dernier front montant valide */
} pd2_journal_t;

/* a appeler a chaque echantillon apres pd2_echantillon : stable = niveau valide, front = son retour (front descendant) */
static inline void pd2_journal_echantillon(pd2_journal_t *j, uint8_t stable, uint8_t front)
{
    if (j->t_periode < 0xFFFFu) j->t_periode++;
    if (stable && !j->prec) j->t_haut = 0u;                  /* front montant valide */
    if (stable && j->t_haut < 0xFFFFu) j->t_haut++;
    if (front) {
        j->periode[j->idx] = j->t_periode; j->haut[j->idx] = j->t_haut;
        j->idx = (uint8_t)((j->idx + 1u) % PD2_JN);
        if (j->n < PD2_JN) j->n++;
        j->seq++;
        j->t_periode = 0u;
    }
    j->prec = stable ? 1u : 0u;
}

/* cadence en tr/min pour une periode en ticks de 4 kHz, 0 si la periode est sous le minimum accepte */
static inline uint16_t pd2_cadence(uint16_t periode)
{
    return periode > PD2_PERIODE_MIN ? (uint16_t)(PD2_CADENCE_NUM / periode) : 0u;
}

#endif /* PEDALIER_PD2_H_ */
