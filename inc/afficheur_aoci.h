/*
 * Afficheur AOCI (build route, 29/10/2026, aoci-firmware-re/validation/porte-4/correctif-afficheur-attendu-20261029.md).
 * Afficheur fige 2-3 min apres le demarrage (meme avec l'extinction automatique sur « jamais ») et extinction par bouton
 * sans etat eteint. Trois parties, fonctions pures testees par tools/test_afficheur_aoci.c :
 *   C1 extinction propre : appui long arme seulement apres un relachement vu, 0x12F83204 = 00 pendant 500 ms, puis
 *      coupure PB5 / PB4 / PB3 ; etat eteint verrouille (MOE refuse) ; rallumage par le bouton si le MCU tourne encore ;
 *   C2 calendrier des trames d'etat du controleur d'origine absentes d'EBiCS (analyse-can.md 1.1 et 1.5) ;
 *   C3 releve des trames recues de l'afficheur (telemetrie E213).
 * Temps en ms (bench_ms, 1 kHz), ecarts calcules en uint32_t : la reboucle a 2^32 ms est sans effet.
 */
#ifndef AFFICHEUR_AOCI_H_
#define AFFICHEUR_AOCI_H_

#include <stdint.h>

/* ---- C1 : extinction par le bouton marche / arret (adc_value[5] < 2800, boucle lente) ---- */
#define EXT_RELACHE_ARME_MS    200u    /* relachement continu avant d'armer l'appui long (demarrage, rallumage) */
#define EXT_APPUI_LONG_MS     2550u    /* appui continu qui eteint (51 boucles lentes de 50 ms, seuil d'EBiCS) */
#define EXT_TRAMES_00_MS       500u    /* 0x12F83204 = 00 avant la coupure (origine, FUN_08017d34) */
#define EXT_RELACHE_ETEINT_MS 1000u    /* eteint : relachement continu avant d'accepter un rallumage */
#define EXT_APPUI_RALLUME_MS   100u    /* eteint : appui continu qui rallume */
#define EXT_DELAI_RALLUME_MS   500u    /* MOE et calibration refuses apres un rallumage (12 V qui s'etablit) */

enum { EXT_ATTENTE = 0, EXT_MARCHE = 1, EXT_EXTINCTION = 2, EXT_ETEINT = 3 };
enum { EXT_RIEN = 0, EXT_DEBUT_EXTINCTION, EXT_COUPER_ALIM, EXT_RALLUMER };

typedef struct {
    uint8_t  etat;               /* EXT_ATTENTE ... EXT_ETEINT */
    uint8_t  appui;              /* dernier echantillon du bouton */
    uint8_t  vu_relache;         /* eteint : relachement de EXT_RELACHE_ETEINT_MS vu */
    uint8_t  rallume;            /* un rallumage a eu lieu (delai EXT_DELAI_RALLUME_MS depuis t_etat) */
    uint32_t t_bouton;           /* instant du dernier changement du bouton */
    uint32_t t_etat;             /* instant d'entree dans l'etat */
} ext_t;

/* au demarrage, bouton suppose tenu : le premier echantillon relache ouvre la fenetre de relachement */
static inline void ext_init(ext_t *e, uint32_t now)
{
    e->etat = EXT_ATTENTE; e->appui = 1u; e->vu_relache = 0u; e->rallume = 0u;
    e->t_bouton = now; e->t_etat = now;
}

/* un pas par boucle lente : renvoie l'action a faire (EXT_RIEN, EXT_DEBUT_EXTINCTION, EXT_COUPER_ALIM, EXT_RALLUMER) */
static inline uint8_t ext_pas(ext_t *e, uint32_t now, uint8_t appui)
{
    appui = appui ? 1u : 0u;
    if (appui != e->appui) { e->appui = appui; e->t_bouton = now; }
    uint32_t d = now - e->t_bouton;                     /* duree de l'etat actuel du bouton */
    switch (e->etat) {
    case EXT_ATTENTE:
        if (!appui && d >= EXT_RELACHE_ARME_MS) e->etat = EXT_MARCHE;
        return EXT_RIEN;
    case EXT_MARCHE:
        if (appui && d >= EXT_APPUI_LONG_MS) { e->etat = EXT_EXTINCTION; e->t_etat = now; e->rallume = 0u; return EXT_DEBUT_EXTINCTION; }
        return EXT_RIEN;
    case EXT_EXTINCTION:
        if (now - e->t_etat >= EXT_TRAMES_00_MS) { e->etat = EXT_ETEINT; e->t_etat = now; e->vu_relache = 0u; return EXT_COUPER_ALIM; }
        return EXT_RIEN;
    default:                                            /* EXT_ETEINT : le MCU tourne encore (cas R2) */
        if (!appui && d >= EXT_RELACHE_ETEINT_MS) e->vu_relache = 1u;
        if (e->vu_relache && appui && d >= EXT_APPUI_RALLUME_MS) {
            e->etat = EXT_ATTENTE; e->t_etat = now; e->vu_relache = 0u; e->rallume = 1u;
            return EXT_RALLUMER;
        }
        return EXT_RIEN;
    }
}

/* vrai si MOE et la calibration sont refuses : extinction, eteint, et EXT_DELAI_RALLUME_MS apres un rallumage */
static inline uint8_t ext_bloque(const ext_t *e, uint32_t now)
{
    if (e->etat == EXT_EXTINCTION || e->etat == EXT_ETEINT) return 1u;
    return (uint8_t)(e->rallume && (uint32_t)(now - e->t_etat) < EXT_DELAI_RALLUME_MS);
}

/* vrai pendant l'extinction et a l'etat eteint : plus de trames periodiques (origine) */
static inline uint8_t ext_coupe(const ext_t *e) { return (uint8_t)(e->etat == EXT_EXTINCTION || e->etat == EXT_ETEINT); }

/* ---- C2 : trames d'etat du controleur d'origine (analyse-can.md 1.1), periodes et 1re emission (instant du cycle) ---- */
enum { AFF_3202 = 0, AFF_3206, AFF_3207, AFF_3204, AFF_1200, AFF_3000, AFF_NB, AFF_3204_00 = AFF_NB };
#define AFF_PERIODES  {100u, 150u, 280u, 1669u, 835u, 10000u}
#define AFF_PHASES    {100u, 150u, 280u,  670u, 870u, 10000u}
#define AFF_00_MS     40u               /* 0x12F83204 = 00 toutes les 40 ms pendant l'extinction */

typedef struct {
    uint32_t prochain[AFF_NB];          /* echeance de la prochaine emission */
    uint32_t t_00;                      /* derniere 0x3204 = 00 */
    uint8_t  n_00;                      /* 0x3204 = 00 deja emises (la premiere part sans attendre) */
    uint32_t vie;                       /* compteur de 0x12F83000 */
} aff_cal_t;

static inline void aff_cal_init(aff_cal_t *c, uint32_t now)
{
    static const uint16_t ph[AFF_NB] = AFF_PHASES;
    for (int i = 0; i < AFF_NB; i++) c->prochain[i] = now + ph[i];
    c->t_00 = now; c->n_00 = 0u;
    /* c->vie garde sa valeur : le compteur de vie ne repart pas a zero au rallumage */
}

/* debut de l'extinction : la premiere 0x3204 = 00 part au tour suivant */
static inline void aff_cal_debut_extinction(aff_cal_t *c) { c->n_00 = 0u; }

/* trame due (AFF_3202 ... AFF_3000, AFF_3204_00) ou -1. coupe : ext_coupe() ; extinction : etat EXT_EXTINCTION ;
   echo_ok : une 0x6300 a deja ete recue (0x3206 n'est qu'un echo). Ne marque rien : voir aff_cal_envoyee. */
static inline int aff_cal_due(const aff_cal_t *c, uint32_t now, uint8_t coupe, uint8_t extinction, uint8_t echo_ok)
{
    if (coupe) {
        if (!extinction) return -1;
        return (c->n_00 == 0u || (uint32_t)(now - c->t_00) >= AFF_00_MS) ? AFF_3204_00 : -1;
    }
    for (int i = 0; i < AFF_NB; i++) {
        if (i == AFF_3206 && !echo_ok) continue;
        if ((int32_t)(now - c->prochain[i]) >= 0) return i;
    }
    return -1;
}

static inline void aff_cal_envoyee(aff_cal_t *c, int i, uint32_t now)
{
    static const uint16_t per[AFF_NB] = AFF_PERIODES;
    if (i == AFF_3204_00) { c->t_00 = now; if (c->n_00 < 255u) c->n_00++; return; }
    if (i < 0 || i >= AFF_NB) return;
    /* cadence tenue sans derive (un retard de quelques tours ne decale pas les suivantes) ; apres une periode entiere de
       retard, repart de maintenant */
    if ((uint32_t)(now - c->prochain[i]) < per[i]) c->prochain[i] += per[i];
    else c->prochain[i] = now + per[i];
    if (i == AFF_3000) c->vie++;
}

/* identifiant etendu : source 0x12, diffusion 0x1F, operation 0 (7 pour l'alarme 0x1200) */
static inline uint32_t aff_id(int i)
{
    static const uint16_t code[AFF_NB + 1] = {0x3202u, 0x3206u, 0x3207u, 0x3204u, 0x1200u, 0x3000u, 0x3204u};
    uint32_t op = (i == AFF_1200) ? 7u : 0u;
    return (0x12u << 24) | (0x1Fu << 19) | (op << 16) | code[i];
}

/* Hall au format de l'origine (0x200003bc = PC6 << 2 | PC7 | PC8 << 1) depuis (GPIOC >> 6) & 7 (b0 PC6, b1 PC7, b2 PC8) */
static inline uint8_t aff_hall_aoci(uint32_t gpio_pc678)
{
    return (uint8_t)(((gpio_pc678 & 1u) << 2) | ((gpio_pc678 >> 1) & 1u) | (((gpio_pc678 >> 2) & 1u) << 1));
}

/* ---- C3 : trames recues de l'afficheur (source 0x13), relevees par processCAN_Rx ---- */
#define AFF_6303_JAMAIS_RECUE 0xFEu

typedef struct {
    uint16_t n;                 /* trames de l'afficheur traitees (reboucle) */
    uint32_t t;                 /* instant de la derniere */
    uint8_t  vu;                /* au moins une trame de l'afficheur */
    uint8_t  vu_6300;           /* au moins une 0x6300 */
    uint8_t  code_6300;         /* dernier octet 1 de 0x6300 (code de niveau brut, echo 0x3206) */
    uint8_t  min_6303;          /* dernier octet 0 de 0x6303 (minutes, 0xFF jamais), AFF_6303_JAMAIS_RECUE avant */
} aff_rx_t;

#define AFF_RX_INIT {0u, 0u, 0u, 0u, 0u, AFF_6303_JAMAIS_RECUE}

/* a appeler pour chaque trame dont la source est l'afficheur (code = 16 bits bas de l'identifiant) */
static inline void aff_rx_note(aff_rx_t *r, uint16_t code, const uint8_t *d, uint8_t dlen, uint32_t now)
{
    r->n++; r->t = now; r->vu = 1u;
    if (code == 0x6300u && dlen >= 2u) { r->code_6300 = d[1]; r->vu_6300 = 1u; }
    if (code == 0x6303u && dlen >= 1u) r->min_6303 = d[0];
}

/* age de la derniere trame de l'afficheur en ms, sature a 65535 (65535 aussi tant qu'aucune n'est recue) */
static inline uint16_t aff_rx_age(const aff_rx_t *r, uint32_t now)
{
    uint32_t a = now - r->t;
    return (uint16_t)((!r->vu || a > 65535u) ? 65535u : a);
}

#endif /* AFFICHEUR_AOCI_H_ */
