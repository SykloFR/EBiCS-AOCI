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

/* 30/10 (correctif-assistance-sans-appui-attendu-20261030.md) : le plancher traduit la voie couple (i_q proportionnel au
   couple / vitesse du MOTEUR). Sa cadence est donc max(cadence du pedalier, erps ramene au pedalier) : moteur entraine
   plus vite que les manivelles (roue libre du pedalier), pas d'amplification. Et il ne s'applique que sous appui reel
   (couple instantane > zero + LOI_OSF_APPUI_MV), jamais sur un couple filtre perime. */
#define LOI_OSF_ERPS_PAR_TRMIN_X10 44   /* erps par tr/min de pedalier x 10 (B1.2, B3 : 4,4 a 5,0 ; 4,4 = sens sur) */
#define LOI_OSF_APPUI_MV           30   /* mV au-dessus du zero de couple */

static inline int32_t loi_osf_plancher_sur(int32_t consigne, int32_t cadence, int32_t erps, int32_t couple_mv, int32_t zero_mv)
{
    if (couple_mv - zero_mv <= LOI_OSF_APPUI_MV) return consigne;
    int32_t cad_moteur = (erps * 10) / LOI_OSF_ERPS_PAR_TRMIN_X10;
    return loi_osf_plancher(consigne, cadence > cad_moteur ? cadence : cad_moteur);
}

#endif /* LOI_OSF_H_ */
