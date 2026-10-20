/*
 * Test hote de svpwm() (porte 4, A1, revue Fangyan point 6). Lance par tools/bench/host_test_svpwm.sh.
 * svpwm() n'est pas recopiee : le script extrait son texte de src/FOC.c (svpwm_extrait.c) et _T, TRIGGER_OFFSET_ADC,
 * ADC_CONV_TICKS, ADC_SETTLE_TICKS, DYNAMIC_ADC_THRESHOLD, U_MAX_BENCH viennent de inc/config.h (build banc).
 * Balayage : tension a l'amplitude maximale (_U_MAX, limite du cercle dans l'ISR) sur 36 000 angles (pas de 0,01 deg),
 * les 6 secteurs. Pour chaque amplitude : CCR max (phase la plus chargee), marge sous DYNAMIC_ADC_THRESHOLD, fenetre basse
 * au sommet avec le temps mort reel DTG = 25 (TIM1_BDTR = 0x0C19 relu sur la carte le 20/10/2026).
 * Code de sortie 1 si l'amplitude du build banc depasse le seuil.
 */
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include "config.h"

typedef int32_t q31_t;
uint16_t switchtime[3];

#include "svpwm_extrait.c"

#define DTG 25

static int sweep(long u_max, int verbose)
{
	int cmax = 0, cmin = _T, worst_deg_x100 = 0, worst_phase = 0, out_of_range = 0;
	for (int k = 0; k < 36000; k++) {
		double th = k * M_PI / 18000.0;
		q31_t ua = (q31_t)lround(u_max * cos(th));
		q31_t ub = (q31_t)lround(u_max * sin(th));
		svpwm(ua, ub);
		for (int p = 0; p < 3; p++) {
			if (switchtime[p] > _T) out_of_range++;
			if (switchtime[p] > cmax) { cmax = switchtime[p]; worst_deg_x100 = k; worst_phase = p; }
			if (switchtime[p] < cmin) cmin = switchtime[p];
		}
	}
	int seuil = DYNAMIC_ADC_THRESHOLD;
	int besoin = ADC_CONV_TICKS + ADC_SETTLE_TICKS;
	int fenetre_code = (_T - TRIGGER_OFFSET_ADC) - cmax;        /* modele du code : debut d'echantillon - CCR max */
	int bas_conducteur = _T - cmax - DTG;                       /* demi-fenetre basse au sommet apres temps mort */
	if (verbose)
		printf("_U_MAX %4ld : CCR %4d..%4d, max a %6.2f deg (phase %d) ; seuil %d -> marge %4d ; fenetre %3d ticks pour %d ; "
		       "bas conducteur apres DTG %3d ticks ; hors [0,_T] : %d\n",
		       u_max, cmin, cmax, worst_deg_x100 / 100.0, worst_phase + 1, seuil, seuil - cmax, fenetre_code, besoin,
		       bas_conducteur, out_of_range);
	return (cmax <= seuil && out_of_range == 0) ? 0 : 1;
}

int main(void)
{
	printf("_T %d, TRIGGER_OFFSET_ADC %d, ADC_CONV_TICKS %d, ADC_SETTLE_TICKS %d, DYNAMIC_ADC_THRESHOLD %d, DTG %d, _SQRT3 %d\n",
	       _T, TRIGGER_OFFSET_ADC, ADC_CONV_TICKS, ADC_SETTLE_TICKS, DYNAMIC_ADC_THRESHOLD, DTG, _SQRT3);
	const long info[] = { 1640L, 1920L };          /* pour comparaison : ancienne proposition et production */
	for (unsigned i = 0; i < sizeof(info) / sizeof(info[0]); i++) sweep(info[i], 1);
	int ko = sweep(U_MAX_BENCH, 1);
	printf("build banc U_MAX_BENCH = %ld : %s\n", (long)U_MAX_BENCH, ko ? "ECHEC (CCR max > seuil ou hors plage)" : "OK (CCR max <= seuil)");
	return ko;
}
