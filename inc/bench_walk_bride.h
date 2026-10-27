/*
 * Bride de vitesse du walk de banc (porte 4, decision de revue du 27/10/2026) : sans aimant de roue sur le home trainer,
 * MS.Speedx100 reste a 0 et la limite de 6 km/h d'EBiCS n'agit pas. La consigne i_q du walk descend lineairement de
 * BRIDE_ERPS_DEBUT a BRIDE_ERPS_FIN (erps electriques), puis 0. Variable de banc separee : MS.Speedx100 n'est pas touche.
 * ui16_erps (filtre IIR par front Hall) reste fige quand le rotor s'arrete : en dessous du seuil 6-step (ui16_timertics
 * >= SIXSTEPTHRESHOLD, rotor lent ou arrete), l'erps est tenu pour nul, sinon une valeur figee bloquerait le redemarrage.
 * Fonctions pures, compilees telles quelles par le test hote tools/test_bench_walk_bride.c.
 */
#ifndef BENCH_WALK_BRIDE_H_
#define BENCH_WALK_BRIDE_H_

#include <stdint.h>

#define BRIDE_ERPS_DEBUT 180u
#define BRIDE_ERPS_FIN   200u

/* erps retenu par la bride : 0 si le rotor est lent ou arrete (erps filtre fige) */
static inline uint32_t bench_erps_bride(uint32_t erps, uint32_t timertics, uint32_t seuil_6step)
{
    return (timertics >= seuil_6step) ? 0u : erps;
}

/* consigne bridee : iq jusqu'a BRIDE_ERPS_DEBUT, rampe lineaire, 0 a partir de BRIDE_ERPS_FIN */
static inline int32_t bench_walk_bride(int32_t iq, uint32_t erps)
{
    if (erps <= BRIDE_ERPS_DEBUT) return iq;
    if (erps >= BRIDE_ERPS_FIN) return 0;
    return (int32_t)((iq * (int32_t)(BRIDE_ERPS_FIN - erps)) / (int32_t)(BRIDE_ERPS_FIN - BRIDE_ERPS_DEBUT));
}

#endif /* BENCH_WALK_BRIDE_H_ */
