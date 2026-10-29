/*
 * Test hote de la chaine de couple du build route (29/10/2026) : zero adaptatif (inc/couple_zero.h, en-tete reel) puis
 * filtre IIR par impulsion PD2 (TQFILTER de inc/config.h, recopie de PAS_processing).
 *   gcc -std=gnu99 -Wall -Wextra -Iinc tools/test_chaine_couple.c -o test_chaine_couple.exe -lm               (avant : 20, 1/16)
 *   gcc -std=gnu99 -Wall -Wextra -DROUTE_BUILD -DBENCH_BUILD -DCURRENT_SIGN=1 -Iinc tools/test_chaine_couple.c ... (route : 48, 1/32)
 * Profils a 48 impulsions par tour, regime etabli sur 5 tours ; consigne = 0,114 x couple filtre (niveau 2, 50 tr/min).
 * Attendu chiffre : validation/porte-4/correctif-chaine-couple-attendu-20261029.md (modele Python independant).
 */
#include <math.h>
#include <stdio.h>
#include "config.h"
#include "couple_zero.h"

static int n_ok = 0, n_ko = 0;
#define CHECK(cond, ...) do { if (cond) { n_ok++; } else { n_ko++; printf("  ECHEC : "); printf(__VA_ARGS__); printf("\n"); } } while (0)

enum { NORMAL, FORT, CONSTANT, REPOS, EXTREME };

static int profil(int p, int i)
{
    double s = fabs(sin(3.14159265358979 * i / 24.0));
    switch (p) {
    case NORMAL:   return (int)(790 + 220 * s);          /* B1.3 E1 : jamais sous ~790 mV */
    case FORT:     return (int)(760 + 1800 * sqrt(s));   /* bosses larges, creux courts (E2 : pointes 2500-3000) */
    case CONSTANT: return 1200;
    case REPOS:    return 754 + (i * 7919) % 36;         /* rotation sans effort 754-789 */
    default:       return (i % 24 < 3) ? 300 : 4900;     /* bornes de la fenetre de validite */
    }
}

typedef struct { double moy, zero_moy; int min, max, cum_max; } bilan_t;

/* chaine de PAS_processing (build route) sur n impulsions ; bilan sur les impulsions [n - 240, n) */
static bilan_t chaine(int p, int n)
{
    couple_zero_t cz = COUPLE_ZERO_INIT;
    int32_t cum = 0;
    bilan_t b = {0, 0, 1 << 30, 0, 0};
    for (int i = 0; i < n; i++) {
        int mv = profil(p, i);
        cum -= cum >> TQFILTER;
        int32_t zero = couple_zero_ajoute(&cz, mv);
        if (mv > zero) cum += mv - zero;
        if (cum > b.cum_max) b.cum_max = cum;
        int f = (int)(cum >> TQFILTER);
        if (i >= n - 240) {
            b.moy += f / 240.0; b.zero_moy += zero / 240.0;
            if (f < b.min) b.min = f;
            if (f > b.max) b.max = f;
        }
    }
    return b;
}

/* reponse : rotation sans effort pendant 200 impulsions, puis pedalage normal ; impulsions pour atteindre 63 % */
static int reponse(void)
{
    couple_zero_t cz = COUPLE_ZERO_INIT;
    int32_t cum = 0;
    int f[600];
    for (int i = 0; i < 600; i++) {
        int mv = i < 200 ? profil(REPOS, i) : profil(NORMAL, i - 200);
        cum -= cum >> TQFILTER;
        int32_t zero = couple_zero_ajoute(&cz, mv);
        if (mv > zero) cum += mv - zero;
        f[i] = (int)(cum >> TQFILTER);
    }
    double fin = 0;
    for (int i = 500; i < 600; i++) fin += f[i] / 100.0;
    for (int i = 200; i < 600; i++) if (f[i] >= 0.63 * fin) return i - 200;
    return -1;
}

#define PROCHE(x, ref, tol) (fabs((double)(x) - (ref)) <= (tol))

int main(void)
{
#ifdef ROUTE_BUILD
    const int route = 1;
#else
    const int route = 0;
#endif
    printf("CZ_N = %u, TQFILTER = %d (%s)\n", CZ_N, TQFILTER, route ? "route" : "avant / banc");
    bilan_t nor = chaine(NORMAL, 480), fort = chaine(FORT, 480), cst = chaine(CONSTANT, 480), rep = chaine(REPOS, 480);
    bilan_t ext = chaine(EXTREME, 480);
    int t63 = reponse();
    printf("normal   : filtre moy %.1f crete-crete %d zero moy %.1f consigne moy %.1f\n", nor.moy, nor.max - nor.min,
           nor.zero_moy, 0.114 * nor.moy);
    printf("fort     : filtre moy %.1f zero moy %.1f\n", fort.moy, fort.zero_moy);
    printf("constant : filtre moy %.1f zero moy %.1f\n", cst.moy, cst.zero_moy);
    printf("repos    : filtre moy %.1f consigne moy %.2f\n", rep.moy, 0.114 * rep.moy);
    printf("reponse 63 %% : %d impulsions (%.2f s a 50 tr/min) ; cumul max (300/4900) %d, borne %d\n", t63, t63 / 40.0,
           ext.cum_max, 4900 << TQFILTER);

    /* valeurs du modele Python (attendu), a +-2 sur le couple filtre et le zero, +-2 impulsions sur la reponse */
    const double m_nor = route ? 126.0 : 106.7, m_pp = route ? 22 : 48, m_fz = route ? 1085.0 : 1489.9,
                 m_ff = route ? 1049.9 : 687.4, m_rep = route ? 16.8 : 12.3;
    const int m_t63 = route ? 14 : 9;
    CHECK(PROCHE(nor.moy, m_nor, 2), "normal : moyenne %.1f (modele %.1f)", nor.moy, m_nor);
    CHECK(PROCHE(nor.max - nor.min, m_pp, 2), "normal : crete-crete %d (modele %.0f)", nor.max - nor.min, m_pp);
    CHECK(PROCHE(fort.zero_moy, m_fz, 2) && PROCHE(fort.moy, m_ff, 2), "fort : zero %.1f filtre %.1f", fort.zero_moy, fort.moy);
    CHECK(cst.moy < 0.5 && PROCHE(cst.zero_moy, 1200, 0.5), "constant : %.1f apres 5 tours (zero %.1f)", cst.moy, cst.zero_moy);
    CHECK(PROCHE(rep.moy, m_rep, 2), "repos : %.1f (modele %.1f)", rep.moy, m_rep);
    CHECK(t63 >= m_t63 - 2 && t63 <= m_t63 + 2, "reponse : %d impulsions (modele %d)", t63, m_t63);
    CHECK(ext.cum_max <= (4900 << TQFILTER), "cumul %d borne par 4900 << TQFILTER", ext.cum_max);
    if (route) {
        /* criteres du correctif, contre l'avant (20, 1/16) : crete-crete 48, moyenne 106,7 */
        CHECK(nor.max - nor.min <= 24, "route : ondulation <= 50 %% de l'avant (%d / 48)", nor.max - nor.min);
        CHECK(nor.moy >= 1.15 * 106.7, "route : moyenne >= +15 %% (%.1f / 106,7)", nor.moy);
        CHECK(0.114 * rep.moy <= 2.5, "route : consigne au repos <= 2,5 (%.2f)", 0.114 * rep.moy);
        CHECK(CZ_N == 48u && TQFILTER == 5, "route : CZ_N 48 et TQFILTER 5");
    } else {
        CHECK(CZ_N == 20u && TQFILTER == 4, "hors route : CZ_N 20 et TQFILTER 4 inchanges");
    }

    printf("== %d verifications OK, %d ECHECS -> %s\n", n_ok, n_ko, n_ko ? "ROUGE" : "VERT");
    return n_ko ? 1 : 0;
}
