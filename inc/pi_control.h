/*
 * Coeur du PI de courant (src/FOC.c, PI_control) en fonction pure, compilee telle quelle par le test hote
 * tools/test_pi_antiwindup.c. Comportement d'origine (REVUE-PUISSANCE P4, entiers Q16) inchange, plus l'anti-windup
 * decide en revue le 27/10/2026 (seance echelle, 10 pts sous charge : u_q reste a 96-144 pendant que i_q vaut 40-50 pts
 * pour une consigne de 10) :
 *   - integration conditionnelle : l'integrale n'avance pas quand la sortie est limitee (amplitude limit_output ou pas
 *     max_step) dans le sens de l'erreur ;
 *   - integrale plafonnee a PI_LIMITE_I_NUM / PI_LIMITE_I_DEN de la limite de sortie (main.c), au lieu de _U_MAX.
 * PI_SANS_ANTIWINDUP (test hote seulement) redonne le comportement anterieur pour comparaison.
 */
#ifndef PI_CONTROL_H_
#define PI_CONTROL_H_

#include <stdint.h>

/* Plafond de l'integrale : 3/4 de la limite de sortie. Walk a vide du 27/10 a 230 erps (42 V) : u_q de 1012 a 1037 pour
   _U_MAX = 1500, soit 69 % de la limite ; 3/4 laisse cette tension a l'integrale seule, le terme P complete au-dela. */
#define PI_LIMITE_I_NUM 3
#define PI_LIMITE_I_DEN 4

static inline int32_t pi_pas(int32_t setpoint, int32_t recent_value, int32_t gain_p, int32_t gain_i, int32_t limit_i,
                             int32_t limit_output, int32_t max_step, int32_t *integral_part, int32_t *out)
{
    /* delta sature pour qu'aucun produit ne depasse 2^30 */
    int32_t gmax = (gain_p > gain_i) ? gain_p : gain_i;
    int32_t dmax = (gmax > 0) ? (0x3FFFFFFF / gmax) : 0x3FFFFFFF;
    int32_t delta = setpoint - recent_value;
    if (delta > dmax) delta = dmax;
    if (delta < -dmax) delta = -dmax;

    int32_t p_q16 = delta * gain_p;
    int32_t lim_q16 = limit_i << 16;
    int32_t integ_avant = *integral_part;
    int32_t integ = integ_avant + delta * gain_i;
    if (integ > lim_q16) integ = lim_q16;
    if (integ < -lim_q16) integ = -lim_q16;

    int32_t sum = p_q16 + integ;
    int32_t target = (sum >= 0) ? (sum >> 16) : -((-sum) >> 16);   /* troncature vers zero */

    int32_t o = *out;
    int32_t limite_haut = 0, limite_bas = 0;
    if (target > o + max_step) { o += max_step; limite_haut = 1; }
    else if (target < o - max_step) { o -= max_step; limite_bas = 1; }
    else o = target;
    if (o > limit_output) { o = limit_output; limite_haut = 1; }
    if (o < -limit_output) { o = -limit_output; limite_bas = 1; }

#ifdef PI_SANS_ANTIWINDUP
    (void)limite_haut; (void)limite_bas; (void)integ_avant;
    *integral_part = integ;
#else
    /* integration conditionnelle : sortie limitee dans le sens de l'erreur -> l'integrale ne bouge pas */
    if ((limite_haut && delta > 0) || (limite_bas && delta < 0)) *integral_part = integ_avant;
    else *integral_part = integ;
#endif
    *out = o;
    return o;
}

#endif /* PI_CONTROL_H_ */
