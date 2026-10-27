/*
 * f4_mode_commun.h : PORTE 4, correctif F4 (retrait du mode commun des trois voies de courant de phase), en fonction pure.
 * Sans dependance materielle : compilee telle quelle par le test hote (tools/test_f4_mode_commun.c) et appelee par l'ISR ADC
 * du build de banc (main.c). PROPOSITION POUR REVUE, non flashee.
 *
 * F4 retire la moyenne des trois voies. Un vrai courant moteur est de somme nulle (pas de neutre) : il n'est pas modifie.
 * Seule la part de somme non nulle (bruit commun, ou voie fausse) est repartie sur les trois voies. C'est sans risque pour un
 * bruit commun de quelques points ; c'est dangereux pour une voie fausse ou saturee : [2000, 0, 0] deviendrait
 * [1334, -667, -667] et le trip verrait un courant plus faible que la lecture. F4 est donc REFUSE (courants laisses intacts,
 * le trip voit la lecture) dans trois cas, testes dans cet ordre :
 *   1. masque != 0b111 : une voie au moins est illisible a cet instant (critere d'EBiCS, voir main.c dyn_adc_state :
 *      CCR de la phase la plus chargee > DYNAMIC_ADC_THRESHOLD, declenchement deplace, cette phase est reconstruite) ;
 *   2. une valeur brute (avant offset) a moins de F4_BRUT_MARGE pts de 0 ou de 4095 : ampli ou ADC en butee, la voie
 *      ne mesure plus ;
 *   3. |somme des trois| > 3 x F4_MC_MAX : le mode commun depasse ce qui a ete mesure comme bruit.
 * Complement indispensable : f4_entrees_trip, sans quoi une voie refusee mais ecartee par la reconstruction d'EBiCS
 * echapperait au trip (voir plus bas).
 */
#ifndef F4_MODE_COMMUN_H_
#define F4_MODE_COMMUN_H_

#include <stdint.h>

#define F4_MASQUE_TOUTES 0x07u   /* bit 0 = ph1 (CH0, PA2), bit 1 = ph2 (CH1, PA3), bit 2 = ph3 (CH2, PA5) */

/* Saturation brute : 48 pts = 2048 - 2000. C'est la saturation du firmware AOCI d'origine (|I| > 32000 sur une echelle de
   16 unites par point, soit 2000 pts de mi-echelle, analyse-commande-moteur.md) et PH_SATURATION_COUNTS de safety.c,
   exprimees en valeur brute pour un zero a 2048 : butee basse <= 48, butee haute >= 4047 (~39 mV des rails a 3,3 V). */
#define F4_BRUT_MAX   4095
#define F4_BRUT_MARGE 48

/* Mode commun : 24 pts. Capture du velo du 26/10 (velo-F3-essaiA-pre0, 128 periodes, u_d 0) : mode commun -19..+14 pts,
   ecart-type 5, somme des trois <= 57 pts. 24 = 19 + 1 ecart-type (~5 ecarts-types) : aucune periode de la capture
   refusee (somme <= 57 < 72). Compare sur la somme (|s| > 72) pour eviter l'arrondi de s / 3. Au-dela, une voie fausse
   d'amplitude > 72 pts est vue entiere par le trip. */
#define F4_MC_MAX 24

typedef enum {
    F4_APPLIQUEE = 0,
    F4_REFUS_MASQUE = 1,
    F4_REFUS_SATURATION = 2,
    F4_REFUS_MODE_COMMUN = 3
} f4_resultat_t;

static inline uint8_t f4_brut_sature(int32_t brut)
{
    return (brut <= F4_BRUT_MARGE) || (brut >= F4_BRUT_MAX - F4_BRUT_MARGE);
}

/* i1..i3 : courants corriges d'offset (points ADC), modifies seulement si F4 est appliquee.
   b1..b3 : valeurs brutes 12 bits de la meme conversion (registre injecte + offset injecte).
   masque : voies lisibles a cet instant (F4_MASQUE_TOUTES = les trois). */
static inline __attribute__((always_inline))
f4_resultat_t f4_mode_commun(int16_t *i1, int16_t *i2, int16_t *i3,
                             int32_t b1, int32_t b2, int32_t b3, uint8_t masque)
{
#ifdef F4_LOGIQUE_ACTUELLE
    /* TEST HOTE SEULEMENT : logique de 713f4dd (moyenne sans garde), pour montrer que le test la rejette */
    (void)b1; (void)b2; (void)b3; (void)masque;
    int16_t mc = (int16_t)((*i1 + *i2 + *i3) / 3);
    *i1 -= mc; *i2 -= mc; *i3 -= mc;
    return F4_APPLIQUEE;
#else
    if (masque != F4_MASQUE_TOUTES) return F4_REFUS_MASQUE;
    if (f4_brut_sature(b1) || f4_brut_sature(b2) || f4_brut_sature(b3)) return F4_REFUS_SATURATION;
    int32_t s = (int32_t)*i1 + *i2 + *i3;
    if (s > 3 * F4_MC_MAX || s < -3 * F4_MC_MAX) return F4_REFUS_MODE_COMMUN;
    int16_t mc = (int16_t)(s / 3);
    *i1 -= mc; *i2 -= mc; *i3 -= mc;
    return F4_APPLIQUEE;
#endif
}

static inline int16_t f4_abs16(int16_t v) { return (v < 0) ? (int16_t)-v : v; }

/* Entrees du trip. EBiCS ecarte toujours une voie mesuree : la phase la plus chargee est reconstruite (etats dyn ADC 2 et 3,
   ph1 ou ph2 = -(somme des deux autres)) et le trip prend ph3 = -(ph1 + ph2) ; en etats 0 et 1, ph3 mesuree n'est pas lue.
   Une voie fausse ecartee ainsi echappe au trip quand F4 est refusee (avec F4 sans garde, elle passait diluee aux 2/3).
   p1, p2 : ph1, ph2 apres reconstruction ; mes : les trois voies mesurees apres F4 (corrigees si F4 appliquee, intactes
   sinon) ; masque : voies lisibles a cette conversion. Si les trois sont lisibles, chaque phase prend la plus grande en
   valeur absolue de la reconstruction et de la mesure : le trip ne peut que voir plus, jamais moins. F4 appliquee, la
   somme est nulle a l'arrondi pres (+-2 pts) : rien ne change. Masque != 0b111 : reconstruction seule, comme EBiCS. */
static inline __attribute__((always_inline))
void f4_entrees_trip(int16_t t[3], int16_t p1, int16_t p2, const int16_t mes[3], uint8_t masque)
{
    t[0] = p1; t[1] = p2; t[2] = (int16_t)(-(p1 + p2));
#ifdef F4_LOGIQUE_ACTUELLE
    (void)mes; (void)masque;
#else
    if (masque != F4_MASQUE_TOUTES) return;
    for (int k = 0; k < 3; k++)
        if (f4_abs16(mes[k]) > f4_abs16(t[k])) t[k] = mes[k];
#endif
}

#endif /* F4_MODE_COMMUN_H_ */
