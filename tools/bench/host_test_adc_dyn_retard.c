/*
 * Test hote de la mesure en mode dynamique AVEC le retard reel entre declenchement et rapports cycliques (porte 4, 30/10/2026,
 * incidents perf-niveau5-12h21, -2, -3 et 15h19). Lance par tools/bench/host_test_adc_dyn_retard.sh (svpwm() extraite de
 * src/FOC.c, en-tetes reels inc/dyn_adc_robuste.h et inc/f4_mode_commun.h).
 *
 * Retard : les CCR S(k) (precharges) ecrits par l'ISR k s'appliquent au bas du compteur si l'ISR finit avant, au sommet
 * sinon ; CC4 (precharge depuis b6021d8) est ecrit juste avant eux. La conversion k+1 voit donc (S(k-1), CC4(k-1)),
 * (S(k-1), CC4(k)) ou (S(k), CC4(k)).
 *
 * Revue du 30/10 (soir) : le modele precedent ne verifiait que le placement, pas le chemin de donnees de l'ISR, et supposait
 * 60 ticks d'etablissement. Les anneaux figes des 4 essais en pic montrent qu'une phase dont le bras bas conduit depuis moins
 * de ~450 ticks (echantillon au sommet) ou ~350 ticks (echantillon deplace) est lue fausse, jusqu'a la butee. Ici :
 *   1. placement (en-tete reel) : aucune phase utilisee hors fenetre, avec les temps d'etablissement mesures ;
 *   2. chaine complete periode par periode : une phase non etablie est lue en butee (pire cas), puis F4, reconstruction
 *      d'EBiCS, f4_entrees_trip, dyn_mesure_periode, trip (seuil 6 periodes et butee immediate), courants donnes a la FOC.
 *      Le chemin de b6021d8 est rejoue aussi : il doit declencher le trip (incident 15h19), le nouveau jamais ;
 *   3. garde de saturation et periodes tenues, cas par cas.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include "config.h"

typedef int32_t q31_t;
uint16_t switchtime[3];

#include "svpwm_extrait.c"
#include "dyn_adc_robuste.h"
#include "f4_mode_commun.h"

#define DTG DYN_DTG
#define T_ECH DYN_T_ECH
#define SAT 2000      /* PH_SATURATION_COUNTS (safety.c) */
#define RAIL 2047

static void sv(long u, double th, uint16_t s[3])
{
	svpwm((q31_t)lround(u * cos(th)), (q31_t)lround(u * sin(th)));
	for (int p = 0; p < 3; p++) s[p] = switchtime[p];
}

static dyn_decision_t decider(const uint16_t sp[3], const uint16_t sn[3])
{
	return dyn_decision_robuste(sp, sn, DYN_SEUIL_ROUTE, TRIGGER_OFFSET_ADC_DYN, TRIGGER_DEFAULT, T_ECH, DYN_STAB_DEFAUT,
	                            DYN_STAB_DYN, DTG, _T);
}

/* temps d'etablissement mesure pour un declenchement donne */
static int stab_de(int cc4) { return cc4 == TRIGGER_DEFAULT ? DYN_STAB_DEFAUT : DYN_STAB_DYN; }

/* 1. placement : pour chaque periode non tenue, les phases utilisees sont etablies dans les trois combinaisons de chargement,
   et la phase reconstruite ne commute pas pendant l'echantillon ; dans chaque periode tenue, les voies declarees etablies le
   sont dans les trois combinaisons. Renvoie le nombre de fautes. */
static int verifier_placement(long u, double d_deg, int *tenues, int *suite_max)
{
	double d = d_deg * M_PI / 180.0;
	int fautes = 0, suite = 0; *tenues = 0; *suite_max = 0;
	uint16_t s2[3], s1[3], s0[3];
	for (int k = 0; k < 36000; k++) {
		double th = k * d + 1e-3;
		sv(u, th - 2 * d, s2); sv(u, th - d, s1); sv(u, th, s0);
		dyn_decision_t dp = decider(s2, s1), dn = decider(s1, s0);
		int ddyn = dp.masque != 0x07, ndyn = dn.masque != 0x07;
		int tenir = dp.tenir || dn.tenir || ((ddyn || ndyn) && dp.etat != dn.etat);
		uint8_t valides = dp.valides & dn.valides, utiles = tenir ? valides : (uint8_t)(dp.masque & dn.masque);
		suite = tenir ? suite + 1 : 0;
		if (suite > *suite_max) *suite_max = suite;
		*tenues += tenir;
		const uint16_t *pwm[3] = {s1, s1, s0};
		const dyn_decision_t *dx[3] = {&dp, &dn, &dn};
		for (int c = 0; c < 3; c++) {
			int tech = dx[c]->cc4 + T_ECH, st = stab_de(dx[c]->cc4);
			for (int p = 0; p < 3; p++)
				if (((utiles >> p) & 1) && dyn_marge(pwm[c], (uint8_t)p, tech, DTG, _T) < st) fautes++;
			if (!tenir && dx[c]->masque != 0x07) {
				int m = (dx[c]->etat == 2) ? 0 : (dx[c]->etat == 3 ? 1 : 2);
				if (tech >= pwm[c][m]) fautes++;
			}
		}
	}
	return fautes;
}

/* 2. chaine complete */
typedef struct { int trips, foc_faux, tenues, rejetees, n; double crete; } chaine_t;

static uint32_t lcg = 12345;
static int alea3(void) { lcg = lcg * 1103515245u + 12345u; return (int)((lcg >> 16) % 3); }

/* ancien = 1 : chemin de donnees de b6021d8 (decision a 60 ticks d'etablissement et seuil 1810, courants tenus releves
   apres la reconstruction, garde avant releve) ; combo < 0 : combinaison de chargement tiree au hasard a chaque periode */
static chaine_t chaine(long u, double d_deg, double I, int ancien, int combo)
{
	chaine_t r = {0, 0, 0, 0, 0, 0.0};
	double d = d_deg * M_PI / 180.0;
	uint16_t s1[3], s0[3];
	dyn_decision_t prec = {TRIGGER_DEFAULT, 1, 0x07, 0, 0x07};
	dyn_mesure_t m = {{0, 0, 0}, 0, 0};
	int16_t val_anc[3] = {0, 0, 0}; uint8_t garde_anc = 0;
	int tenir = 0, trip_count = 0; uint8_t valides = 0x07, masque = 0x07, etat = 1;
	uint16_t cc4_prec = TRIGGER_DEFAULT, cc4 = TRIGGER_DEFAULT;
	sv(u, -d, s1);
	for (int k = 0; k < 20000; k++) {
		double th = k * d + 1e-3;
		sv(u, th, s0);
		/* --- ISR k : decision (route_dyn_placer) sur S(k-1) = s1 et S(k) = s0 --- */
		dyn_decision_t dn = ancien ? dyn_decision_robuste(s1, s0, DYNAMIC_ADC_THRESHOLD, TRIGGER_OFFSET_ADC_DYN, TRIGGER_DEFAULT,
		                                                    T_ECH, 60, 60, DTG, _T)
		                           : decider(s1, s0);
		int ddyn = prec.masque != 0x07, ndyn = dn.masque != 0x07;
		tenir = dn.tenir || prec.tenir || ((ddyn || ndyn) && dn.etat != prec.etat);
		valides = dn.valides & prec.valides; masque = dn.masque & prec.masque; etat = dn.etat;
		cc4_prec = prec.cc4; cc4 = dn.cc4;
		prec = dn;
		/* --- conversion k+1 : combinaison reelle --- */
		int c = combo >= 0 ? combo : alea3();
		const uint16_t *s = (c == 2) ? s0 : s1;
		uint16_t cc = (c == 0) ? cc4_prec : cc4;
		double thc = th + d;                             /* angle au moment de la conversion suivante */
		int16_t vrai[3], lu[3];
		for (int p = 0; p < 3; p++) {
			vrai[p] = (int16_t)lround(I * cos(thc - p * 2.0 * M_PI / 3.0));
			int etabli = dyn_marge(s, (uint8_t)p, cc + T_ECH, DTG, _T) >= stab_de(cc);
			lu[p] = etabli ? vrai[p] : (int16_t)(((k + p) & 1) ? RAIL : -RAIL);
		}
		/* --- ISR k+1 : F4, reconstruction d'EBiCS, trip --- */
		int16_t i1 = lu[0], i2 = lu[1], i3 = lu[2];
		f4_mode_commun(&i1, &i2, &i3, lu[0] + 2048, lu[1] + 2048, lu[2] + 2048, masque);
		const int16_t mes[3] = {i1, i2, i3};
		if (etat == 2) i1 = (int16_t)(-i2 - i3);
		if (etat == 3) i2 = (int16_t)(-i1 - i3);
		int16_t t[3]; uint8_t res = DYN_MESUREE;
		int16_t foc1 = i1, foc2 = i2; int foc = 1;
		if (!ancien) {
			f4_entrees_trip(t, i1, i2, mes, masque);
			res = dyn_mesure_periode(&m, (uint8_t)tenir, valides, t, mes, SAT, PH_CURRENT_TRIP, DYN_SUITE_MAX);
			foc = res == DYN_MESUREE;
		} else {
			if (tenir) { i1 = val_anc[0]; i2 = val_anc[1]; i3 = val_anc[2]; }
			else { val_anc[0] = i1; val_anc[1] = i2; val_anc[2] = i3; }
			f4_entrees_trip(t, i1, i2, tenir ? val_anc : mes, tenir ? F4_MASQUE_TOUTES : masque);
			int16_t mx = 0, mv = 0;
			for (int q = 0; q < 3; q++) { if (abs(t[q]) > mx) mx = (int16_t)abs(t[q]); if (abs(val_anc[q]) > mv) mv = (int16_t)abs(val_anc[q]); }
			if (mx >= SAT && !garde_anc && mv < PH_CURRENT_TRIP) {
				garde_anc = 1; t[0] = val_anc[0]; t[1] = val_anc[1]; t[2] = val_anc[2];
				i1 = val_anc[0]; i2 = val_anc[1];
			} else garde_anc = 0;
			foc1 = i1; foc2 = i2;
			res = tenir ? DYN_TENUE : DYN_MESUREE;
		}
		r.n++;
		r.tenues += res == DYN_TENUE; r.rejetees += res == DYN_REJETEE;
		int16_t mx = 0;
		for (int q = 0; q < 3; q++) if (abs(t[q]) > mx) mx = (int16_t)abs(t[q]);
		if (mx > r.crete) r.crete = mx;
		/* trip (safety.c) */
		if (mx >= SAT) r.trips++;
		if (mx >= PH_CURRENT_TRIP) { if (++trip_count >= PH_TRIP_PERIODS) r.trips++; } else trip_count = 0;
		/* courants donnes a la FOC : ph1, ph2 justes a l'arrondi de F4 pres (tolerance 3 pts) */
		if (foc && (abs(foc1 - vrai[0]) > 3 || abs(foc2 - vrai[1]) > 3)) r.foc_faux++;
		for (int p = 0; p < 3; p++) s1[p] = s0[p];
	}
	return r;
}

/* 3. garde et periodes tenues, cas par cas */
static int verifier_garde(void)
{
	int ko = 0;
	dyn_mesure_t m = {{0, 0, 0}, 0, 0};
	int16_t t[3], mes[3] = {0, 0, 0};
#define PER(ten, val, a, b, c) do { t[0] = (a); t[1] = (b); t[2] = (c); mes[0] = (a); mes[1] = (b); mes[2] = (c); \
	                               r = dyn_mesure_periode(&m, (ten), (val), t, mes, SAT, PH_CURRENT_TRIP, DYN_SUITE_MAX); } while (0)
	uint8_t r;
	PER(0, 7, 90, -40, -50);   if (r != DYN_MESUREE || m.val[0] != 90) { printf("garde : mesure normale KO\n"); ko = 1; }
	PER(0, 7, -2047, -40, 2087); if (r != DYN_REJETEE || t[0] != 90) { printf("garde : butee isolee non rejetee\n"); ko = 1; }
	PER(0, 7, 95, -45, -50);   if (r != DYN_MESUREE || m.garde) { printf("garde : rearmement KO\n"); ko = 1; }
	/* butee, periodes tenues, butee : la seconde passe au trip */
	PER(0, 7, 2047, -45, -2002); if (r != DYN_REJETEE) { printf("garde : 1re butee non rejetee\n"); ko = 1; }
	PER(1, 4, 0, 0, -60);      if (r != DYN_TENUE || t[2] != -60 || t[0] != 95) { printf("garde : periode tenue KO (voies)\n"); ko = 1; }
	PER(0, 7, 2047, -45, -2002); if (r != DYN_MESUREE || t[0] < SAT) { printf("garde : 2e butee apres tenue masquee\n"); ko = 1; }
	/* voie etablie en butee pendant une periode tenue : rejetee une fois, puis passe */
	m.garde = 0; m.val[0] = 50; m.val[1] = -20; m.val[2] = -30;
	PER(1, 4, 0, 0, -2047);    if (r != DYN_REJETEE) { printf("garde : butee en periode tenue non rejetee\n"); ko = 1; }
	PER(1, 4, 0, 0, -2047);    if (r != DYN_TENUE || t[2] > -SAT) { printf("garde : 2e butee en periode tenue masquee\n"); ko = 1; }
	/* surintensite reelle : derniere valeur valide au-dessus du seuil de trip -> la butee passe */
	m.garde = 0; m.suite = 0; m.val[0] = 260; m.val[1] = -130; m.val[2] = -130;
	PER(0, 7, 2047, -1000, -1047); if (r != DYN_MESUREE || t[0] < SAT) { printf("garde : surintensite reelle masquee\n"); ko = 1; }
	/* suite bornee : DYN_SUITE_MAX periodes tenues, puis la mesure passe */
	m.garde = 0; m.suite = 0;
	int n = 0;
	for (int i = 0; i < DYN_SUITE_MAX + 3; i++) { PER(1, 0, 10, 10, -20); if (r == DYN_TENUE) n++; else break; }
	if (n != DYN_SUITE_MAX) { printf("garde : suite tenue non bornee (%d)\n", n); ko = 1; }
	printf("garde et periodes tenues (9 cas) : %s\n", ko ? "ECHEC" : "OK");
	return ko;
#undef PER
}

int main(void)
{
	int ko = 0;
	printf("temps d'etablissement : %d ticks au sommet, %d en declenchement deplace ; seuil dynamique %d ; U_MAX %ld\n",
	       DYN_STAB_DEFAUT, DYN_STAB_DYN, DYN_SEUIL_ROUTE, (long)U_MAX_BENCH);
	/* 1. placement, de 0,5 a 15 deg par periode, plusieurs tensions */
	const long us[] = {1200L, 1400L, 1600L, U_MAX_BENCH};
	for (unsigned i = 0; i < 4; i++)
		for (double dd = 0.5; dd <= 15.0; dd += 0.5) {
			int t, sm; int f = verifier_placement(us[i], dd, &t, &sm);
			if (f) ko = 1;
			if (f || (int)(dd * 2) % 6 == 0 || dd == 6.0)
				printf("placement U %ld, %4.1f deg/periode : %d fautes, %5.1f %% tenues, suite max %d\n", us[i], dd, f, 100.0 * t / 36000, sm);
		}
	/* 2. chaine complete, i_q au plafond (98 pts) */
	int ko2 = 0;
	for (unsigned i = 0; i < 4; i++)
		for (double dd = 3.0; dd <= 15.0; dd += 1.0)
			for (int combo = -1; combo < 3; combo++) {
				chaine_t c = chaine(us[i], dd, 98.0, 0, combo);
				int bad = c.trips || c.foc_faux || c.crete > 98.0 * 1.05;
				if (bad) ko2 = 1;
				if (bad || (combo < 0 && (dd == 6.0 || dd == 12.0)))
					printf("chaine U %ld, %4.1f deg, combinaison %2d : %d trips, %d periodes FOC fausses, crete %.0f (i 98), %.1f %% tenues, %d butees rejetees%s\n",
					       us[i], dd, combo, c.trips, c.foc_faux, c.crete, 100.0 * c.tenues / c.n, c.rejetees, bad ? "  <-- ECHEC" : "");
			}
	ko |= ko2;
	chaine_t a = chaine(U_MAX_BENCH, 6.0, 98.0, 1, -1);
	printf("chemin b6021d8 rejoue (U %ld, 6 deg, i 98) : %d trips, %d periodes FOC fausses, crete %.0f -> %s\n", (long)U_MAX_BENCH,
	       a.trips, a.foc_faux, a.crete, a.trips ? "declenche (comme au velo le 30/10, 15h19)" : "NE DECLENCHE PAS : modele a revoir");
	if (!a.trips) ko = 1;
	ko |= verifier_garde();
	printf("build route : %s\n", ko ? "ECHEC" : "OK (placement etabli, chaine sans trip ni courant faux pour la FOC, garde conforme)");
	return ko;
}
