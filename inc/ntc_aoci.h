/*
 * Temperature du controleur (sonde PB1) par la table NTC du firmware d'origine AOCI (build route, 29/10/2026,
 * validation/porte-4/correctif-ntc-controleur-attendu-20261029.md). Table copiee du dump brut
 * (aoci-firmware-re/validation/porte-1/dump1.bin, adresse 0x0801B206, referencee par le litteral a 0x08011BB4) :
 * 191 valeurs ADC 10 bits, indice i -> (i - 40) degres C, CTN vers la masse (la valeur baisse quand ca chauffe).
 * Points 35 a 37 hors ordre dans l'original (912, 874, 902), copies tels quels : ~1 degre d'erreur entre -5 et -3 degres C.
 * Fonction pure, testee par tools/test_ntc_aoci.c.
 */
#ifndef NTC_AOCI_H_
#define NTC_AOCI_H_

#include <stdint.h>

#define NTC_AOCI_N 191u
static const uint16_t ntc_aoci_table[NTC_AOCI_N] = {
    1007u, 1006u, 1005u, 1004u, 1003u, 1001u, 1000u,  999u,  997u,  995u,  994u,  992u,  990u,  988u,  986u,  984u,
     982u,  979u,  977u,  974u,  971u,  968u,  965u,  962u,  959u,  956u,  952u,  948u,  944u,  940u,  936u,  932u,
     927u,  922u,  917u,  912u,  874u,  902u,  896u,  890u,  884u,  878u,  872u,  865u,  858u,  851u,  844u,  837u,
     829u,  820u,  814u,  806u,  797u,  789u,  780u,  772u,  763u,  754u,  745u,  735u,  726u,  716u,  707u,  697u,
     687u,  677u,  667u,  657u,  647u,  636u,  626u,  616u,  605u,  595u,  585u,  574u,  564u,  553u,  543u,  533u,
     522u,  512u,  502u,  492u,  482u,  472u,  462u,  452u,  442u,  433u,  423u,  414u,  405u,  396u,  387u,  378u,
     369u,  360u,  352u,  344u,  335u,  327u,  319u,  312u,  304u,  297u,  289u,  282u,  275u,  268u,  262u,  255u,
     249u,  242u,  236u,  230u,  224u,  219u,  213u,  208u,  202u,  197u,  192u,  187u,  182u,  178u,  173u,  168u,
     164u,  160u,  156u,  152u,  148u,  144u,  140u,  137u,  133u,  130u,  127u,  123u,  120u,  117u,  114u,  111u,
     108u,  106u,  103u,  100u,   98u,   95u,   93u,   91u,   88u,   86u,   84u,   82u,   80u,   78u,   76u,   74u,
      72u,   70u,   69u,   67u,   65u,   64u,   62u,   61u,   60u,   58u,   57u,   56u,   55u,   53u,   52u,   51u,
      50u,   49u,   47u,   46u,   45u,   44u,   43u,   42u,   41u,   40u,   39u,   39u,   38u,   37u,   36u
};

/* ADC 12 bits -> dixiemes de degre C, interpole, sature a -40,0 et 150,0. Recherche depuis le cote chaud : le point hors
   ordre (874) ne capte ainsi que les valeurs 902..911 (-5 a -3 degres C) au lieu de 874..911 (-5 a +1). */
static inline int16_t ntc_aoci_dixiemes(uint16_t adc12)
{
    uint32_t k;
    if ((uint32_t)adc12 >= 4u * ntc_aoci_table[0]) return -400;
    if ((uint32_t)adc12 <= 4u * ntc_aoci_table[NTC_AOCI_N - 1u]) return 1500;
    for (k = NTC_AOCI_N - 1u; k >= 1u; k--) {
        uint32_t haut = 4u * ntc_aoci_table[k - 1u], bas = 4u * ntc_aoci_table[k];
        if (bas <= adc12 && haut > adc12)
            return (int16_t)((int32_t)(k - 1u) * 10 + (int32_t)(10u * (haut - adc12) / (haut - bas)) - 400);
    }
    return -400;
}

/* degres C arrondis (MS.int_Temperature) */
static inline int16_t ntc_aoci_c(uint16_t adc12)
{
    int16_t t = ntc_aoci_dixiemes(adc12);
    return (int16_t)(t >= 0 ? (t + 5) / 10 : -((-t + 5) / 10));
}

#endif /* NTC_AOCI_H_ */
