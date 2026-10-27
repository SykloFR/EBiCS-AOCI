/*
 * Test hote de la cadence sur PD2 (inc/pedalier_pd2.h, l'en-tete reel compile dans le firmware).
 *   gcc -std=gnu99 -Wall -Wextra -Iinc tools/test_pedalier_pd2.c -o test_pedalier_pd2.exe
 * Anti-rebond 2 ms (8 echantillons a 4 kHz), un front descendant par impulsion, cadence 5000 / periode.
 */
#include <stdio.h>
#include <stdlib.h>
#include "pedalier_pd2.h"

static int n_ok = 0, n_ko = 0;
#define CHECK(cond, ...) do { if (cond) { n_ok++; } else { n_ko++; printf("  ECHEC : "); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* applique n echantillons du niveau v, renvoie le nombre de fronts valides */
static int tenir(pd2_antirebond_t *s, int v, int n)
{
    int f = 0;
    while (n--) f += pd2_echantillon(s, (uint8_t)v);
    return f;
}

/* une impulsion : bas pendant b echantillons puis haut pendant h, avec r echantillons de rebond a chaque front */
static int impulsion(pd2_antirebond_t *s, int b, int h, int r)
{
    int f = 0;
    for (int k = 0; k < r; k++) f += pd2_echantillon(s, (uint8_t)(k & 1));   /* rebond autour du front descendant */
    f += tenir(s, 0, b);
    for (int k = 0; k < r; k++) f += pd2_echantillon(s, (uint8_t)!(k & 1)); /* rebond autour du front montant */
    f += tenir(s, 1, h);
    return f;
}

int main(void)
{
    /* 1. amorce : aucun front au premier echantillon, ni en restant au meme niveau */
    for (int v = 0; v < 2; v++) {
        pd2_antirebond_t s = {0, 0, 0};
        CHECK(tenir(&s, v, 100) == 0 && s.stable == v, "amorce %d", v);
    }

    /* 2. une impulsion propre : exactement un front, au 8e echantillon bas */
    {
        pd2_antirebond_t s = {0, 0, 0};
        tenir(&s, 1, 20);
        int f7 = tenir(&s, 0, 7);
        int f8 = tenir(&s, 0, 1);
        int reste = tenir(&s, 0, 50) + tenir(&s, 1, 50);
        CHECK(f7 == 0 && f8 == 1 && reste == 0, "impulsion propre : %d %d %d", f7, f8, reste);
    }

    /* 3. pepins de 1 a 7 echantillons ignores, dans les deux sens */
    for (int g = 1; g < (int)PD2_ANTIREBOND_ECH; g++) {
        pd2_antirebond_t s = {0, 0, 0};
        tenir(&s, 1, 20);
        int f = tenir(&s, 0, g) + tenir(&s, 1, 20);
        CHECK(f == 0 && s.stable == 1, "pepin bas de %d echantillons : %d front(s)", g, f);
        f = 0;
        tenir(&s, 0, 20);                 /* un front valide ici, non compte */
        f = tenir(&s, 1, g) + tenir(&s, 0, 20);
        CHECK(f == 0 && s.stable == 0, "pepin haut de %d echantillons : %d front(s)", g, f);
    }

    /* 4. un tour : 48 impulsions avec rebonds de 0 a 7 echantillons a chaque front, cadences 20 a 200 tr/min. Domaine :
          chaque niveau tenu au moins 8 echantillons hors rebond (limite physique d'un anti-rebond de 2 ms, comme
          l'origine) ; a 200 tr/min un demi-niveau dure 12 echantillons, donc rebonds de 4 au plus. */
    for (int rpm = 20; rpm <= 200; rpm += 10) {
        int periode = (int)(4000L * 60 / (PD2_IMPULSIONS_TOUR * rpm));   /* ticks */
        for (int r = 0; r < 8; r++) {
            if (periode / 2 - r < (int)PD2_ANTIREBOND_ECH) continue;
            pd2_antirebond_t s = {0, 0, 0};
            tenir(&s, 1, 50);
            int f = 0;
            for (int p = 0; p < (int)PD2_IMPULSIONS_TOUR; p++) f += impulsion(&s, periode / 2 - r, periode - periode / 2 - r, r);
            CHECK(f == (int)PD2_IMPULSIONS_TOUR, "%d tr/min, rebonds %d : %d fronts au lieu de 48", rpm, r, f);
        }
    }

    /* 5. sequences aleatoires : jamais deux fronts a moins de 2 x 8 echantillons d'intervalle */
    srand(3);
    for (int t = 0; t < 200; t++) {
        pd2_antirebond_t s = {0, 0, 0};
        long dernier = -1000, ecart_min = 1000000;
        for (long i = 0; i < 20000; i++) {
            if (pd2_echantillon(&s, (uint8_t)(rand() % 5 == 0 ? !s.stable : s.stable ^ (rand() % 13 == 0)))) {
                if (i - dernier < ecart_min) ecart_min = i - dernier;
                dernier = i;
            }
        }
        CHECK(ecart_min >= 2 * (long)PD2_ANTIREBOND_ECH, "aleatoire %d : ecart min %ld", t, ecart_min);
    }

    /* 6. cadence : 5000 / periode, 0 sous le minimum accepte */
    CHECK(PD2_CADENCE_NUM == 5000u, "PD2_CADENCE_NUM %u", PD2_CADENCE_NUM);
    CHECK(pd2_cadence(83) == 60, "83 ticks : %u", pd2_cadence(83));
    CHECK(pd2_cadence(42) == 119, "42 ticks : %u", pd2_cadence(42));
    CHECK(pd2_cadence(167) == 29, "167 ticks (un tour en 2 s) : %u", pd2_cadence(167));
    CHECK(pd2_cadence(21) == 238, "21 ticks : %u", pd2_cadence(21));
    CHECK(pd2_cadence(20) == 0 && pd2_cadence(0) == 0, "minimum : %u %u", pd2_cadence(20), pd2_cadence(0));
    CHECK(pd2_cadence(64000) == 0, "arret : %u", pd2_cadence(64000));
    for (unsigned p = 21; p < 65535; p++)
        if (pd2_cadence((uint16_t)p) > 255) { CHECK(0, "cadence > 255 a %u ticks", p); break; }
    n_ok++;   /* balayage 21..65534 : cadence toujours sur 8 bits (MS.cadence) */

    /* 7. journal des periodes (E210) : impulsions regulieres, ordre chronologique, saturation, rejets */
    {
        pd2_antirebond_t s = {0, 0, 0};
        static pd2_journal_t j;
        int i, f;
        for (i = 0; i < 100; i++) { f = pd2_echantillon(&s, 1); pd2_journal_echantillon(&j, s.stable, (uint8_t)f); }
        /* 100 impulsions : bas 30 echantillons, haut 50 (periode 80), pepin haut de 5 echantillons au milieu du bas */
        for (int p = 0; p < 100; p++) {
            for (i = 0; i < 30; i++) {
                int brut = (i >= 12 && i < 17) ? 1 : 0;
                f = pd2_echantillon(&s, (uint8_t)brut); pd2_journal_echantillon(&j, s.stable, (uint8_t)f);
            }
            for (i = 0; i < 50; i++) { f = pd2_echantillon(&s, 1); pd2_journal_echantillon(&j, s.stable, (uint8_t)f); }
        }
        CHECK(j.n == PD2_JN && j.seq == 100, "journal : n %u seq %u", j.n, j.seq);
        int ok = 1;
        for (unsigned r = 0; r < PD2_JN; r++) {
            unsigned k = (j.idx + r) % PD2_JN;
            if (j.periode[k] != 80 || j.haut[k] != 50) { ok = 0; printf("  rang %u : %u %u\n", r, j.periode[k], j.haut[k]); break; }
        }
        CHECK(ok, "journal : periodes 80 et durees hautes 50 sur les 64 rangs");
        /* arret de 20 s puis une impulsion : periode saturee, duree haute saturee */
        for (long t = 0; t < 80000; t++) { f = pd2_echantillon(&s, 1); pd2_journal_echantillon(&j, s.stable, (uint8_t)f); }
        for (i = 0; i < 20; i++) { f = pd2_echantillon(&s, 0); pd2_journal_echantillon(&j, s.stable, (uint8_t)f); }
        unsigned dern = (j.idx + PD2_JN - 1) % PD2_JN;
        CHECK(j.periode[dern] == 0xFFFF && j.haut[dern] == 0xFFFF && j.seq == 101,
              "saturation : %u %u seq %u", j.periode[dern], j.haut[dern], j.seq);
        /* ordre chronologique : periodes 40, 41, ..., 139 ; les 64 dernieres en ordre */
        static pd2_journal_t j2;
        pd2_antirebond_t s2 = {0, 0, 0};
        for (i = 0; i < 50; i++) { f = pd2_echantillon(&s2, 1); pd2_journal_echantillon(&j2, s2.stable, (uint8_t)f); }
        for (int p = 0; p < 100; p++) {
            int per = 40 + p;
            for (i = 0; i < 20; i++) { f = pd2_echantillon(&s2, 0); pd2_journal_echantillon(&j2, s2.stable, (uint8_t)f); }
            for (i = 0; i < per - 20; i++) { f = pd2_echantillon(&s2, 1); pd2_journal_echantillon(&j2, s2.stable, (uint8_t)f); }
        }
        ok = 1;
        for (unsigned r = 1; r < PD2_JN; r++) {
            unsigned k = (j2.idx + r) % PD2_JN, k0 = (j2.idx + r - 1) % PD2_JN;
            if (j2.periode[k] != j2.periode[k0] + 1 || j2.haut[k] != j2.periode[k] - 20) { ok = 0; break; }
        }
        CHECK(ok, "ordre chronologique et duree basse 20 sur 64 rangs");
    }

    printf("== %d verifications OK, %d ECHECS -> %s\n", n_ok, n_ko, n_ko ? "ROUGE" : "VERT");
    return n_ko ? 1 : 0;
}
