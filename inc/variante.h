/*
 * Variante de mesure de courant (octet de configuration 0x0803D800, correctif F2) : 2 -> R = 1,5 (3,14 pts/A),
 * 1 -> R = 3,5 (7,33 pts/A), autre -> refus (defaut 10). Repli du build route (decision du 27/10/2026, apres le flash de
 * l'image route l'octet s'est lu 0xFF) : un octet VIERGE (0xFF) est traite comme la variante mesuree sur ce controleur,
 * ROUTE_VARIANTE_REPLI, si et seulement si le repli est autorise. Fonction pure, testee par tools/test_variante.c.
 */
#ifndef VARIANTE_H_
#define VARIANTE_H_

#include <stdint.h>

/* renvoie la variante a appliquer (1 ou 2), 0 si refusee ; *repli = 1 si le repli a servi */
static inline uint8_t variante_effective(uint8_t lu, uint8_t repli_autorise, uint8_t *repli)
{
    *repli = 0u;
    if (lu == 2u || lu == 1u) return lu;
    if (lu == 0xFFu && (repli_autorise == 1u || repli_autorise == 2u)) { *repli = 1u; return repli_autorise; }
    return 0u;
}

#endif /* VARIANTE_H_ */
