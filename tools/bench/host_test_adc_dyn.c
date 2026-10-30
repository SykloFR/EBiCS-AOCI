/*
 * Test hote de la mesure des courants de phase en forte modulation (porte 4, lot B3, 30/10/2026). Lance par
 * tools/bench/host_test_adc_dyn.sh, qui extrait svpwm() de src/FOC.c (comme host_test_svpwm). dyn_adc_state() est recopiee
 * de src/main.c (placement du declenchement CC4).
 *
 * Modele temporel d'une periode (compteur centre, _T = 2000 ticks de 66 MHz par demi-periode), d'apres la configuration :
 *  - phases en PWM0 : bras haut tant que CNT < CCR, bras bas (shunt mesure) au-dela, avec le temps mort DTG = 25 a la mise
 *    en conduction ; temps t = CNT a la montee, 2 _T - CNT a la descente ;
 *  - ADC : TRGO = O3CPRE (CH3 en PWM1), front montant a la montee quand CNT passe CC4 ; echantillonnage 7,5 cycles ADC x 6
 *    = 45 ticks, la valeur est celle de la FIN de l'echantillonnage (t_ech = CC4 + 45) ;
 *  - une phase est mesurable si son bras bas conduit a t_ech depuis au moins ADC_SETTLE_TICKS (60, estimation) ;
 *  - mode normal : CC4 = _T - 10, trois phases mesurees ; mode dynamique (CCR max > DYNAMIC_ADC_THRESHOLD) :
 *    CC4 = CCR max - 10, la phase la plus chargee est reconstruite (-(a+b)), les deux autres sont mesurees.
 * Sorties par amplitude : part des angles en mode dynamique, marge de stabilisation minimale des phases mesurees, et
 * « calme avant l'echantillon » : temps depuis le dernier front d'une phase quelconque avant t_ech (un front recent peut
 * perturber les shunts voisins).
 */
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include "config.h"

typedef int32_t q31_t;
uint16_t switchtime[3];

#include "svpwm_extrait.c"

#define DTG 25
#define T_ECH ((7 * 6) + 3)   /* 7,5 cycles ADC x 6 = 45 ticks */

typedef struct { int dyn_pct10, marge_min, calme_min_norm, calme_min_dyn, calme_lt30_pct10, inmesurable; } bilan_t;

static bilan_t balayer(long u, int off_dyn, int verbose)
{
	bilan_t b = {0, 100000, 100000, 100000, 0, 0};
	int n = 36000, ndyn = 0, nlt30 = 0;
	for (int k = 0; k < n; k++) {
		double th = k * M_PI / 18000.0;
		svpwm((q31_t)lround(u * cos(th)), (q31_t)lround(u * sin(th)));
		int imax = 0;
		for (int p = 1; p < 3; p++) if (switchtime[p] > switchtime[imax]) imax = p;
		int dyn = switchtime[imax] > DYNAMIC_ADC_THRESHOLD;
		int cc4 = dyn ? switchtime[imax] - off_dyn : TRIGGER_DEFAULT;
		int tech = cc4 + T_ECH;
		ndyn += dyn;
		/* phases mesurees : marge = t_ech - (CCR + DTG) ; encore conductrice si t_ech <= 2 _T - CCR */
		for (int p = 0; p < 3; p++) {
			if (dyn && p == imax) continue;
			int marge = tech - (switchtime[p] + DTG);
			if (tech > 2 * _T - switchtime[p]) marge = -1;
			if (marge < b.marge_min) b.marge_min = marge;
			if (marge < ADC_SETTLE_TICKS) b.inmesurable++;
		}
		/* dernier front (toutes phases) avant t_ech */
		int calme = 100000;
		for (int p = 0; p < 3; p++) {
			int fronts[4] = { switchtime[p], switchtime[p] + DTG, 2 * _T - switchtime[p], 2 * _T - switchtime[p] + DTG };
			for (int f = 0; f < 4; f++) if (fronts[f] <= tech && tech - fronts[f] < calme) calme = tech - fronts[f];
		}
		if (dyn) { if (calme < b.calme_min_dyn) b.calme_min_dyn = calme; if (calme < 30) nlt30++; }
		else if (calme < b.calme_min_norm) b.calme_min_norm = calme;
	}
	b.dyn_pct10 = ndyn * 1000 / n;
	b.calme_lt30_pct10 = nlt30 * 1000 / n;
	if (verbose)
		printf("_U_MAX %4ld, avance dyn %3d : dynamique %3d.%d %% des angles ; marge mini des phases mesurees %4d ticks (%d cas < %d) ; "
		       "calme avant l'echantillon : normal >= %d, dynamique >= %d ticks (%d.%d %% des angles < 30)\n",
		       u, off_dyn, b.dyn_pct10 / 10, b.dyn_pct10 % 10, b.marge_min, b.inmesurable, ADC_SETTLE_TICKS,
		       b.calme_min_norm == 100000 ? -1 : b.calme_min_norm, b.calme_min_dyn == 100000 ? -1 : b.calme_min_dyn,
		       b.calme_lt30_pct10 / 10, b.calme_lt30_pct10 % 10);
	return b;
}

int main(void)
{
	printf("_T %d, DYNAMIC_ADC_THRESHOLD %d, TRIGGER_OFFSET_ADC %d, echantillonnage %d ticks, DTG %d, stabilisation %d\n",
	       _T, DYNAMIC_ADC_THRESHOLD, TRIGGER_OFFSET_ADC, T_ECH, DTG, ADC_SETTLE_TICKS);
	const long us[] = { 1500L, 1640L, 1700L, 1800L, 1850L, 1920L };
	for (unsigned i = 0; i < sizeof(us) / sizeof(us[0]); i++) balayer(us[i], TRIGGER_OFFSET_ADC, 1);
	/* variante : declenchement dynamique avance pour que l'echantillon se termine AVANT la commutation de la phase la
	   plus chargee (avance > 45 ticks d'echantillonnage) */
	const int avances[] = { 65, 80, 100 };
	const long ud[] = { 1800L, 1850L, 1920L };
	for (unsigned a = 0; a < 3; a++) for (unsigned i = 0; i < 3; i++) balayer(ud[i], avances[a], 1);
#ifdef ROUTE_BUILD
	/* verdict du build route : a U_MAX_BENCH et TRIGGER_OFFSET_ADC_DYN, toutes les phases mesurees stabilisees depuis
	   ADC_SETTLE_TICKS et aucun front a moins de ADC_SETTLE_TICKS avant l'echantillon, en mode normal comme dynamique */
	bilan_t r = balayer(U_MAX_BENCH, TRIGGER_OFFSET_ADC_DYN, 1);
	int ok = r.inmesurable == 0 && r.calme_min_norm >= ADC_SETTLE_TICKS
	         && (r.calme_min_dyn == 100000 || r.calme_min_dyn >= ADC_SETTLE_TICKS);
	/* 30/10 soir : la route n'utilise plus dyn_adc_state (depuis 8295942) ; son verdict est dans host_test_adc_dyn_retard
	   (placement robuste, temps d'etablissement mesures, periodes tenues). Ici, information sur la regle d'EBiCS seule. */
	printf("regle d'EBiCS seule a U_MAX_BENCH = %ld, avance dynamique %d : %s (information ; verdict route : host_test_adc_dyn_retard)\n",
	       (long)U_MAX_BENCH, TRIGGER_OFFSET_ADC_DYN, ok ? "mesure stabilisee et au calme" : "phases mesurees hors fenetre");
	return 0;
#else
	return 0;
#endif
}
