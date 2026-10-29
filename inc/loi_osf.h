/*
 * Loi d'assistance type OSF hybride (build route, 29/10/2026, loi-type-osf-attendu-20261029.md).
 * Le preset HYBRID du Z8-OSF prend le max d'une voie puissance (P_bat proportionnel a P_pedale) et d'une voie couple
 * (P_bat proportionnel au couple) qui se croisent vers 40 tr/min. Pour un moteur pedalier, la voie puissance est la loi
 * EBiCS (i_q proportionnel au couple filtre) ; la voie couple revient a i_q proportionnel au couple / cadence. Le max
 * des deux = consigne x max(1, 40 / cadence), cadence bornee a 10 tr/min (x 4 au plus), avant l'ecretage au plafond du
 * niveau. Fonction pure, testee par tools/test_loi_osf.c.
 */
#ifndef LOI_OSF_H_
#define LOI_OSF_H_

#include <stdint.h>

#define LOI_OSF_CADENCE_CROISEMENT 40   /* tr/min : croisement des voies puissance et couple du Z8 (0,745 x m) */
#define LOI_OSF_CADENCE_MIN        10   /* tr/min : borne du plancher, soit x 4 au plus */

static inline int32_t loi_osf_plancher(int32_t consigne, int32_t cadence)
{
    if (consigne <= 0 || cadence >= LOI_OSF_CADENCE_CROISEMENT) return consigne;
    if (cadence < LOI_OSF_CADENCE_MIN) cadence = LOI_OSF_CADENCE_MIN;
    return (consigne * LOI_OSF_CADENCE_CROISEMENT) / cadence;
}

#endif /* LOI_OSF_H_ */
