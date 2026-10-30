/*
 * Placement de la mesure des courants en mode dynamique, robuste au retard d'une periode (build route, 30/10/2026,
 * correctif-mesure-dynamique-attendu-20261030.md). Fonctions pures, testees par tools/bench/host_test_adc_dyn_retard.c.
 *
 * Les CCR sont precharges : ecrits par l'ISR, ils s'appliquent au BAS du compteur si l'ISR finit avant, au SOMMET sinon.
 * La conversion suivante voit donc S(k) ou S(k-1) selon la duree de l'ISR. EBiCS placait CC4 d'apres S(k-1) seulement :
 * a erps 284 (6,2 deg par periode), un quart des echantillons tombaient sur une commutation (incidents du 30/10).
 * Ici, la decision vaut pour S(k-1) ET S(k) : phase reconstruite = la plus chargee dans les deux, CC4 = min des deux -
 * avance, phases mesurees etablies dans les deux cas. Sinon la periode est declaree non mesurable (tenir = 1).
 *
 * Revue du 30/10 (soir) : etablissement de la mesure mesure sur les anneaux figes des 4 essais en pic (E20D, ajustement
 * sinusoidal par anneau) : une phase dont le bras bas conduit depuis moins de ~350 ticks (echantillon deplace) ou ~450 ticks
 * (echantillon au sommet) avant la fin de l'echantillon est lue fausse (jusqu'a la butee). EBiCS supposait 60. D'ou deux
 * temps d'etablissement : stab (declenchement par defaut) et stab_dyn (declenchement deplace).
 */
#ifndef DYN_ADC_ROBUSTE_H_
#define DYN_ADC_ROBUSTE_H_

#include <stdint.h>

typedef struct {
    uint16_t cc4;      /* declenchement de la prochaine conversion */
    uint8_t etat;      /* char_dyn_adc_state d'EBiCS : 1 phase C reconstruite, 2 phase A, 3 phase B */
    uint8_t masque;    /* voies lisibles pour F4 (bit 0 = ph1) */
    uint8_t tenir;     /* 1 : prochaine periode non mesurable */
    uint8_t valides;   /* voies etablies avec les deux jeux de rapports (bit 0 = ph1), meme si la periode est tenue */
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

/* calme : aucune commutation (bras haut qui s'ouvre a s, bras bas qui se ferme a s + dtg) dans les `calme` ticks avant la
   fin de l'echantillon, pour les deux jeux de rapports (30/10 soir : crete E100 a 2-4 x i_q sous 1309f72 et 7478f7d, voies
   lues au sommet juste apres la commutation de la phase la plus chargee, aux frontieres de secteur) */
static inline uint8_t dyn_calme(const uint16_t s[3], int32_t tech, int32_t dtg, int32_t calme)
{
    for (uint8_t p = 0; p < 3; p++)
        if ((int32_t)s[p] < tech && (int32_t)s[p] + dtg > tech - calme) return 0;
    return 1;
}

static inline dyn_decision_t dyn_decision_robuste(const uint16_t sp[3], const uint16_t sn[3], int32_t seuil, int32_t avance,
                                                  int32_t defaut, int32_t t_ech, int32_t stab, int32_t stab_dyn, int32_t dtg,
                                                  int32_t t, int32_t calme)
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
    } else {
        int32_t lo = sp[m] < sn[m] ? sp[m] : sn[m];
        d.cc4 = (uint16_t)(dyn ? lo - avance : defaut);
        d.masque = dyn ? masque_de[m] : 0x07;
    }
    int32_t tech = (int32_t)d.cc4 + t_ech;
    int32_t st = (d.cc4 == (uint16_t)defaut) ? stab : stab_dyn;
    d.valides = 0;
    for (uint8_t p = 0; p < 3; p++)
        if (dyn_marge(sp, p, tech, dtg, t) >= st && dyn_marge(sn, p, tech, dtg, t) >= st) d.valides |= (uint8_t)(1u << p);
    if (!dyn_calme(sp, tech, dtg, calme) || !dyn_calme(sn, tech, dtg, calme)) d.valides = 0;   /* commutation trop proche */
    if ((d.valides & d.masque) != d.masque) d.tenir = 1;   /* une phase mesuree n'est pas etablie */
    if (d.masque != 0x07 && (tech >= sp[m] || tech >= sn[m])) d.tenir = 1;   /* la phase reconstruite commuterait */
    return d;
}

/* Chemin de donnees d'une periode (revue du 30/10 soir : en b6021d8, les derniers courants valides gardaient la phase 3
   brute en etat 1 et etaient releves AVANT la garde de saturation, qui ne pouvait donc jamais rejeter un echantillon).
   t : en entree, entrees du trip calculees sur l'echantillon (f4_entrees_trip) ; en sortie, entrees du trip retenues.
   mes : les trois voies apres F4. tenue : periode declaree non mesurable ; valides : voies etablies malgre tout.
   Periode tenue (au plus suite_max de suite) : le trip prend les voies etablies et, pour les autres, les dernieres entrees
   valides ; la FOC ne doit pas utiliser l'echantillon (PI suspendu). Garde : un echantillon en butee (>= sat) alors que les
   dernieres entrees valides etaient sous le seuil de trip est rejete une fois ; seule une periode mesuree non saturee
   rearme la garde, donc deux butees separees par des periodes tenues declenchent toujours le trip.
   Les dernieres entrees valides ne sont relevees que sur une periode mesuree acceptee. */
#define DYN_MESUREE 0u
#define DYN_TENUE   1u
#define DYN_REJETEE 2u

typedef struct {
    int16_t val[3];     /* entrees du trip de la derniere periode mesuree acceptee */
    uint8_t garde;      /* 1 : une butee a deja ete rejetee depuis la derniere periode mesuree non saturee */
    uint8_t suite;      /* periodes tenues consecutives */
} dyn_mesure_t;

static inline int16_t dyn_abs16(int16_t v) { return (v < 0) ? (int16_t)-v : v; }

static inline uint8_t dyn_mesure_periode(dyn_mesure_t *s, uint8_t tenue, uint8_t valides, int16_t t[3], const int16_t mes[3],
                                         int16_t sat, int16_t seuil, uint8_t suite_max)
{
    uint8_t r = DYN_MESUREE;
    if (tenue && s->suite < suite_max) {
        s->suite++;
        for (uint8_t p = 0; p < 3; p++) t[p] = ((valides >> p) & 1u) ? mes[p] : s->val[p];
        r = DYN_TENUE;
    } else {
        s->suite = 0;
    }
    int16_t mx = 0, mv = 0;
    for (uint8_t p = 0; p < 3; p++) {
        if (dyn_abs16(t[p]) > mx) mx = dyn_abs16(t[p]);
        if (dyn_abs16(s->val[p]) > mv) mv = dyn_abs16(s->val[p]);
    }
    if (mx >= sat && !s->garde && mv < seuil) {
        s->garde = 1;
        t[0] = s->val[0]; t[1] = s->val[1]; t[2] = s->val[2];
        return DYN_REJETEE;
    }
    if (r == DYN_MESUREE) {
        if (mx < sat) s->garde = 0;
        s->val[0] = t[0]; s->val[1] = t[1]; s->val[2] = t[2];
    }
    return r;
}

#endif /* DYN_ADC_ROBUSTE_H_ */
