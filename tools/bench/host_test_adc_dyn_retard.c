/*
 * Test hote de la mesure en mode dynamique AVEC le retard reel entre declenchement et rapports cycliques (porte 4, 30/10/2026,
 * incidents perf-niveau5-12h21 et -2 : saturation de l'ADC sur une phase mesuree, a fort courant, a U_MAX 1800).
 * Lance par tools/bench/host_test_adc_dyn_retard.sh (svpwm() extraite de src/FOC.c).
 *
 * L'ISR ADC (main.c) appelle dyn_adc_state() AVANT FOC_calculation() : CC4 (sans prechargement, effet immediat) est place
 * d'apres les rapports S(k-1), et l'etat de reconstruction aussi. Les CCR (precharges) S(k) ecrits ensuite sont charges a
 * l'evenement de mise a jour suivant : au BAS du compteur si l'ISR finit avant (cas A : la conversion suivante voit S(k) avec
 * un CC4 calcule sur S(k-1)), au SOMMET sinon (cas B : coherent). La duree de l'ISR varie : les deux cas existent.
 * Entre S(k-1) et S(k), l'angle avance de d (6,2 deg a erps 284, plus les sauts aux fronts Hall).
 *
 * Compare : code actuel (cas A) et correctif « robuste » : decision prise sur S(k-1) ET S(k) (phase masquee = la plus chargee
 * dans les deux, CC4 = min des deux - avance, phases mesurees valides pour les deux) ; sinon la periode est declaree non
 * mesurable (courants de la periode precedente gardes, trip sur la derniere mesure valide).
 */
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include "config.h"

typedef int32_t q31_t;
uint16_t switchtime[3];

#include "svpwm_extrait.c"
#include "dyn_adc_robuste.h"

#define DTG 25
#define T_ECH 45

static void sv(long u, double th, int s[3])
{
	svpwm((q31_t)lround(u * cos(th)), (q31_t)lround(u * sin(th)));
	for (int p = 0; p < 3; p++) s[p] = switchtime[p];
}
static int imax3(const int s[3]) { int m = 0; for (int p = 1; p < 3; p++) if (s[p] > s[m]) m = p; return m; }

/* marge de la phase p mesuree a t_ech avec les rapports reels s : temps de conduction du bras bas avant l'echantillon */
static int marge(const int s[3], int p, int tech)
{
	if (tech > 2 * _T - s[p]) return -1;
	return tech - (s[p] + DTG);
}

typedef struct { int min_marge, n_mauvais, n_tenues, n; } res_t;

static res_t balayer(long u, double d_deg, int avance, int robuste)
{
	res_t r = {100000, 0, 0, 0};
	double d = d_deg * M_PI / 180.0;
	for (int k = 0; k < 36000; k++) {
		double th = k * M_PI / 18000.0;
		int sp[3], sn[3];
		sv(u, th - d, sp);   /* S(k-1) : sert au placement de CC4 dans le code actuel */
		sv(u, th, sn);       /* S(k)   : rapports appliques pendant la conversion (cas A) */
		int m, dyn, cc4;
		r.n++;
		if (!robuste) {
			m = imax3(sp); dyn = sp[m] > DYNAMIC_ADC_THRESHOLD;
			cc4 = dyn ? sp[m] - avance : TRIGGER_DEFAULT;
		} else {
			int mp = imax3(sp), mn = imax3(sn);
			dyn = sp[mp] > DYNAMIC_ADC_THRESHOLD || sn[mn] > DYNAMIC_ADC_THRESHOLD;
			if (dyn && mp != mn) { r.n_tenues++; continue; }      /* phase la plus chargee differente : non mesurable */
			m = mp;
			int lo = sp[m] < sn[m] ? sp[m] : sn[m];
			cc4 = dyn ? lo - avance : TRIGGER_DEFAULT;
		}
		int tech = cc4 + T_ECH, mauvais = 0, mm = 100000;
		for (int p = 0; p < 3; p++) {
			if (dyn && p == m) continue;
			int a = marge(sn, p, tech), b = marge(sp, p, tech);
			int w = robuste ? (a < b ? a : b) : a;       /* robuste : valide pour les deux jeux de rapports */
			if (w < mm) mm = w;
		}
		/* la phase reconstruite doit avoir son bras haut encore ferme a t_ech (sinon elle commute pendant l'echantillon) */
		if (dyn && (tech >= sn[m] || (robuste && tech >= sp[m]))) mm = -1;
		if (robuste && mm < ADC_SETTLE_TICKS) { r.n_tenues++; continue; }
		if (mm < ADC_SETTLE_TICKS) mauvais = 1;
		if (mm < r.min_marge) r.min_marge = mm;
		r.n_mauvais += mauvais;
	}
	return r;
}

/* verification de l'en-tete reel (inc/dyn_adc_robuste.h) : pour chaque periode non tenue, les phases mesurees ont leur marge
   avec les DEUX jeux de rapports (cas A : S(k) applique, cas B : S(k-1) applique), et la phase reconstruite ne commute
   pas pendant l'echantillon ; renvoie le nombre de periodes fausses et compte les periodes tenues */
static int verifier_entete(long u, double d_deg, int *tenues)
{
	double d = d_deg * M_PI / 180.0;
	int fausses = 0; *tenues = 0;
	for (int k = 0; k < 36000; k++) {
		double th = k * M_PI / 18000.0;
		int a[3], b[3];
		sv(u, th - d, a); sv(u, th, b);
		uint16_t sp[3] = {a[0], a[1], a[2]}, sn[3] = {b[0], b[1], b[2]};
		dyn_decision_t x = dyn_decision_robuste(sp, sn, DYNAMIC_ADC_THRESHOLD, TRIGGER_OFFSET_ADC_DYN, TRIGGER_DEFAULT,
		                                        T_ECH, ADC_SETTLE_TICKS, DTG, _T);
		if (x.tenir) { (*tenues)++; continue; }
		int tech = x.cc4 + T_ECH, m = (x.etat == 2) ? 0 : (x.etat == 3 ? 1 : 2), dyn = x.masque != 0x07;
		for (int cas = 0; cas < 2; cas++) {
			const int *s = cas ? b : a;
			for (int p = 0; p < 3; p++) {
				if (dyn && p == m) continue;
				if (marge(s, p, tech) < ADC_SETTLE_TICKS) fausses++;
			}
			if (dyn && tech >= s[m]) fausses++;
		}
	}
	return fausses;
}

int main(void)
{
	printf("modele avec retard d'une periode entre CC4 et CCR (cas A), U_MAX et avances du build route\n");
	const double ds[] = {0.0, 3.0, 6.2, 9.0, 12.0};
	const long us[] = {1640L, 1700L, 1800L};
	for (unsigned i = 0; i < 3; i++)
		for (unsigned j = 0; j < 5; j++) {
			res_t a = balayer(us[i], ds[j], TRIGGER_OFFSET_ADC_DYN, 0);
			res_t b = balayer(us[i], ds[j], TRIGGER_OFFSET_ADC_DYN, 1);
			printf("U %ld, pas %4.1f deg : actuel marge mini %5d, %5d angles mauvais (%.2f %%) | robuste marge mini %5d, %d mauvais, "
			       "%d tenus (%.2f %%)\n", us[i], ds[j], a.min_marge, a.n_mauvais, 100.0 * a.n_mauvais / a.n,
			       b.min_marge, b.n_mauvais, b.n_tenues, 100.0 * b.n_tenues / b.n);
		}
	/* verdict : en-tete reel, U_MAX du build route, pas de 0 a 15 deg par periode */
	int ko = 0;
	for (double dd = 0.0; dd <= 15.0; dd += 0.5) {
		int t; int f = verifier_entete(U_MAX_BENCH, dd, &t);
		if (f) ko = 1;
		if ((int)(dd * 2) % 6 == 0) printf("en-tete reel, U %ld, pas %4.1f deg : %d periodes fausses, %d tenues (%.2f %%)\n",
		                                  (long)U_MAX_BENCH, dd, f, t, 100.0 * t / 36000);
	}
	printf("build route : %s\n", ko ? "ECHEC (periode mesuree hors fenetre)" : "OK (aucune periode mesuree hors fenetre, de 0 a 15 deg par periode)");
	return ko;
}
