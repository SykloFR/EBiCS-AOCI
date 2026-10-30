/*
 * Placement de la mesure des courants en mode dynamique, robuste au retard d'une periode (build route, 30/10/2026,
 * correctif-mesure-dynamique-attendu-20261030.md). Fonction pure, testee par tools/bench/host_test_adc_dyn_retard.c.
 *
 * Les CCR sont precharges : ecrits par l'ISR, ils s'appliquent au BAS du compteur si l'ISR finit avant, au SOMMET sinon.
 * La conversion suivante voit donc S(k) ou S(k-1) selon la duree de l'ISR. EBiCS placait CC4 d'apres S(k-1) seulement :
 * a erps 284 (6,2 deg par periode), un quart des echantillons tombaient sur une commutation (incidents du 30/10).
 * Ici, la decision vaut pour S(k-1) ET S(k) : phase reconstruite = la plus chargee dans les deux, CC4 = min des deux -
 * avance, phases mesurees stabilisees dans les deux cas. Sinon la periode est declaree non mesurable (tenir = 1) : l'ISR
 * suivante garde les derniers courants valides.
 */
#ifndef DYN_ADC_ROBUSTE_H_
#define DYN_ADC_ROBUSTE_H_

#include <stdint.h>

typedef struct {
    uint16_t cc4;      /* declenchement de la prochaine conversion */
    uint8_t etat;      /* char_dyn_adc_state d'EBiCS : 1 phase C reconstruite, 2 phase A, 3 phase B */
    uint8_t masque;    /* voies lisibles pour F4 (bit 0 = ph1) */
    uint8_t tenir;     /* 1 : prochaine periode non mesurable, garder les derniers courants valides */
} dyn_decision_t;

static inline uint8_t dyn_imax(const uint16_t s[3])
{
    uint8_t m = 0;
    if (s[1] > s[m]) m = 1;
    if (s[2] > s[m]) m = 2;
    return m;
}

/* marge de la phase p : temps de conduction du bras bas avant la fin de l'echantillonnage (negatif : non conductrice) */
static inline int32_t dyn_marge(const uint16_t s[3], uint8_t p, int32_t tech, int32_t dtg, int32_t t)
{
    if (tech > 2 * t - (int32_t)s[p]) return -1;
    return tech - ((int32_t)s[p] + dtg);
}

static inline dyn_decision_t dyn_decision_robuste(const uint16_t sp[3], const uint16_t sn[3], int32_t seuil, int32_t avance,
                                                  int32_t defaut, int32_t t_ech, int32_t stab, int32_t dtg, int32_t t)
{
    static const uint8_t etat_de[3] = {2, 3, 1};            /* phase la plus chargee -> etat EBiCS */
    static const uint8_t masque_de[3] = {0x06, 0x05, 0x03}; /* phase illisible si deplace */
    dyn_decision_t d;
    uint8_t mp = dyn_imax(sp), mn = dyn_imax(sn);
    uint8_t dyn = (sp[mp] > seuil) || (sn[mn] > seuil);
    uint8_t m = dyn ? mp : mn;
    d.etat = etat_de[m];
    d.tenir = 0;
    if (dyn && mp != mn) {                                  /* phase la plus chargee differente : non mesurable */
        d.cc4 = (uint16_t)defaut; d.masque = 0x07; d.tenir = 1;
        return d;
    }
    int32_t lo = sp[m] < sn[m] ? sp[m] : sn[m];
    d.cc4 = (uint16_t)(dyn ? lo - avance : defaut);
    d.masque = dyn ? masque_de[m] : 0x07;
    int32_t tech = (int32_t)d.cc4 + t_ech;
    for (uint8_t p = 0; p < 3; p++) {
        if (dyn && p == m) continue;
        if (dyn_marge(sp, p, tech, dtg, t) < stab || dyn_marge(sn, p, tech, dtg, t) < stab) d.tenir = 1;
    }
    if (dyn && (tech >= sp[m] || tech >= sn[m])) d.tenir = 1;   /* la phase reconstruite commuterait pendant l'echantillon */
    return d;
}

#endif /* DYN_ADC_ROBUSTE_H_ */
