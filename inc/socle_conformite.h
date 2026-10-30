/*
 * Socle de conformite (build route, 28/10/2026) : regles street du Z8-OSF transposees a l'AOCI 36 V 15 A
 * (validation/porte-4/socle-conformite-attendu-20261028.md). Niveau = indice EBiCS 0..5 (level_to_array_element).
 * Fonctions pures, testees par tools/test_socle_conformite.c.
 */
#ifndef SOCLE_CONFORMITE_H_
#define SOCLE_CONFORMITE_H_

#include <stdint.h>

/* R2 : plafond batterie par niveau, % du courant batterie maximal (Z8 36 V : 36/60/72/84/100 %). Le niveau 0 garde la
   valeur du niveau 1 : sans assistance, un plafond nul ferait reguler le courant batterie a 0 (BC_limit_flag). */
static const uint8_t socle_ibat_pct[6] = {36u, 36u, 60u, 72u, 84u, 100u};

static inline int32_t socle_ibat_niveau_mA(uint8_t niveau, int32_t ibat_max_mA)
{
    if (niveau > 5u) niveau = 5u;
    return ibat_max_mA * socle_ibat_pct[niveau] / 100;
}

/* R3 : plafond de phase = rapport_x100 / 100 x courant batterie du niveau, en points (pts_par_A_x100 / 100 pts par A),
   arrondi par defaut, ecrete a ph_max_pts ; 0 au niveau 0 (pas d'assistance, comme assist_settings[0][0]).
   Rapport : 1,75 (regle P29 du Z8, 48 V) jusqu'au 30/10 ; 2,3 en route depuis (SOCLE_PHASE_SUR_BAT_X100, config.h) */
static inline int32_t socle_phase_niveau_pts(uint8_t niveau, int32_t ibat_mA, int32_t pts_par_A_x100, int32_t ph_max_pts,
                                             int32_t rapport_x100)
{
    int64_t p;
    if (niveau == 0u) return 0;
    p = (int64_t)ibat_mA * rapport_x100 * pts_par_A_x100 / 10000000;
    return p > ph_max_pts ? ph_max_pts : (int32_t)p;
}

/* R5 : timer du niveau 5. Cumul du temps passe avec L5 demande (meme moteur arrete) ; a SOCLE_L5_MAX, L4 impose pendant
   SOCLE_L5_REPOS, puis cumul remis a zero. Temps en ticks de 4 kHz reels. */
#define SOCLE_L5_MAX_TICKS   (120u * 4000u)    /* 2 min */
#define SOCLE_L5_REPOS_TICKS (480u * 4000u)    /* 8 min */

typedef struct {
    uint32_t cumul;      /* ticks avec L5 demande depuis la derniere fin de repos */
    uint32_t repos;      /* ticks ecoules dans le repos en cours */
    uint8_t  bloque;     /* 1 : repos en cours, L5 refuse */
} socle_l5_t;

#define SOCLE_L5_INIT {0u, 0u, 0u}

/* avance de dt ticks avec le niveau demande, renvoie le niveau applique */
static inline uint8_t socle_l5_pas(socle_l5_t *t, uint32_t dt, uint8_t demande)
{
    if (t->bloque) {
        t->repos += dt;
        if (t->repos >= SOCLE_L5_REPOS_TICKS) { t->bloque = 0u; t->repos = 0u; t->cumul = 0u; }
        else return demande == 5u ? 4u : demande;
    }
    if (demande == 5u) {
        t->cumul += dt;
        if (t->cumul >= SOCLE_L5_MAX_TICKS) { t->bloque = 1u; t->repos = 0u; return 4u; }
    }
    return demande;
}

/* codes assist_level EBiCS <-> indice 0..5 (afficheur 5 niveaux : 2, 4, 6, 8, 9) */
static const uint8_t socle_code_niveau[6] = {0u, 2u, 4u, 6u, 8u, 9u};

/* R1 : rampe de bride, pedalier en rotation : consigne pleine sous (limite - 2 km/h), nulle au-dessus de (limite + 2),
   lineaire entre les deux (vitesses en km/h x 100) */
static inline int32_t socle_bride(int32_t consigne, int32_t v_x100, int32_t limite_x100)
{
    int32_t bas = limite_x100 - 200, haut = limite_x100 + 200;
    if (v_x100 <= bas) return consigne;
    if (v_x100 >= haut) return 0;
    return consigne * (haut - v_x100) / (haut - bas);
}

#endif /* SOCLE_CONFORMITE_H_ */
