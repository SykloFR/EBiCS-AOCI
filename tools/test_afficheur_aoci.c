/*
 * Test hote de l'afficheur AOCI (inc/afficheur_aoci.h, l'en-tete reel compile dans le firmware).
 *   gcc -std=gnu99 -Wall -Wextra -Iinc tools/test_afficheur_aoci.c -o test_afficheur_aoci.exe
 *   (et avec -DROUTE_BUILD : meme resultat, l'en-tete ne depend pas du build)
 * Valeurs attendues : aoci-firmware-re/validation/porte-4/correctif-afficheur-attendu-20261029.md, section 5.
 */
#include <stdio.h>
#include "afficheur_aoci.h"

static int n_ok = 0, n_ko = 0;
#define CHECK(cond, ...) do { if (cond) { n_ok++; } else { n_ko++; printf("  ECHEC : "); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define PAS_LENT 50u              /* boucle lente : un echantillon du bouton toutes les 50 ms */

/* bouton appuye entre [a0, a1[ (ms depuis t0), deux appuis possibles */
typedef struct { uint32_t a0, a1, b0, b1; } scenario_t;
static uint8_t appuye(const scenario_t *s, uint32_t t) { return (t >= s->a0 && t < s->a1) || (t >= s->b0 && t < s->b1); }

/* deroule le scenario de t0 a t0 + duree ; note l'instant (relatif) de chaque action, -1 si absente */
static void deroule(ext_t *e, uint32_t t0, uint32_t duree, const scenario_t *s, long act[4])
{
    for (int k = 0; k < 4; k++) act[k] = -1;
    for (uint32_t t = 0; t <= duree; t += PAS_LENT) {
        uint8_t a = ext_pas(e, t0 + t, appuye(s, t));
        if (a != EXT_RIEN && act[a] < 0) act[a] = (long)t;
    }
}

static void test_extinction(uint32_t t0)
{
    long act[4];
    /* 1. bouton tenu 10 s des le demarrage (rallumage par appui long) : jamais d'extinction, armement apres le relachement */
    { ext_t e; ext_init(&e, t0); scenario_t s = {0, 10000, 0, 0}; deroule(&e, t0, 12000, &s, act);
      CHECK(act[EXT_DEBUT_EXTINCTION] < 0 && e.etat == EXT_MARCHE, "appui tenu au demarrage : extinction a %ld, etat %u", act[EXT_DEBUT_EXTINCTION], e.etat); }
    /* 2. armement : 150 ms de relachement ne suffisent pas, 200 ms oui */
    { ext_t e; ext_init(&e, t0); ext_pas(&e, t0, 0); ext_pas(&e, t0 + 150, 0);
      CHECK(e.etat == EXT_ATTENTE, "arme trop tot (150 ms)");
      ext_pas(&e, t0 + 200, 0); CHECK(e.etat == EXT_MARCHE, "pas arme a 200 ms"); }
    /* 3. appui long : 2500 ms ne suffisent pas, 2550 ms oui ; coupure 500 ms apres */
    { ext_t e; ext_init(&e, t0); scenario_t s = {1000, 3500, 0, 0}; deroule(&e, t0, 6000, &s, act);
      CHECK(act[EXT_DEBUT_EXTINCTION] < 0, "extinction sur 2500 ms d'appui (%ld)", act[EXT_DEBUT_EXTINCTION]); }
    { ext_t e; ext_init(&e, t0); scenario_t s = {1000, 3600, 0, 0}; deroule(&e, t0, 6000, &s, act);
      CHECK(act[EXT_DEBUT_EXTINCTION] == 3550, "extinction a %ld au lieu de 3550", act[EXT_DEBUT_EXTINCTION]);
      CHECK(act[EXT_COUPER_ALIM] == 4050, "coupure a %ld au lieu de 4050", act[EXT_COUPER_ALIM]);
      CHECK(e.etat == EXT_ETEINT, "etat %u apres la coupure", e.etat); }
    /* 4. un echantillon relache au milieu remet le compte a zero */
    { ext_t e; ext_init(&e, t0); scenario_t s = {1000, 2500, 2550, 4500}; deroule(&e, t0, 6000, &s, act);
      CHECK(act[EXT_DEBUT_EXTINCTION] < 0, "relachement de 50 ms ignore (%ld)", act[EXT_DEBUT_EXTINCTION]); }
    /* 5. verrou : extinction, eteint, 500 ms apres le rallumage ; pas avant (temps relatifs r, appel a t0 + r) */
    { ext_t e; ext_init(&e, t0); uint32_t r = 0; uint8_t a, rall = 0, ext2 = 0;
#define PAS(b) ext_pas(&e, t0 + r, (b))
      for (; r < 1000; r += PAS_LENT) PAS(0);
      CHECK(!ext_bloque(&e, t0 + r) && !ext_coupe(&e), "bloque en marche");
      for (; r <= 1000 + 2550; r += PAS_LENT) PAS(1);
      CHECK(e.etat == EXT_EXTINCTION && ext_bloque(&e, t0 + r) && ext_coupe(&e), "extinction : etat %u", e.etat);
      for (; r <= 1000 + 2550 + 500; r += PAS_LENT) PAS(1);
      CHECK(e.etat == EXT_ETEINT && ext_bloque(&e, t0 + r) && ext_coupe(&e), "eteint : etat %u", e.etat);
      /* eteint, bouton encore tenu 3 s : pas de rallumage tant que le relachement d'1 s n'est pas vu */
      for (uint32_t k = 0; k < 3000; k += PAS_LENT, r += PAS_LENT) if (PAS(1) == EXT_RALLUMER) rall = 1;
      CHECK(!rall, "rallume sans relachement");
      /* relache 950 ms puis appui : refuse */
      for (uint32_t k = 0; k < 950; k += PAS_LENT, r += PAS_LENT) PAS(0);
      for (uint32_t k = 0; k < 300; k += PAS_LENT, r += PAS_LENT) if (PAS(1) == EXT_RALLUMER) rall = 1;
      CHECK(!rall && e.etat == EXT_ETEINT, "rallume apres 950 ms de relachement");
      /* relache 1 s puis appui : 50 ms ne suffisent pas, 100 ms oui */
      for (uint32_t k = 0; k <= 1000; k += PAS_LENT, r += PAS_LENT) PAS(0);
      uint32_t ra = r;
      a = PAS(1); r += PAS_LENT; CHECK(a == EXT_RIEN, "rallume a 0 ms d'appui");
      a = PAS(1); r += PAS_LENT; CHECK(a == EXT_RIEN, "rallume a 50 ms d'appui");
      a = PAS(1); CHECK(a == EXT_RALLUMER && r - ra == 100, "rallumage %u a %u ms", a, (unsigned)(r - ra));
      CHECK(e.etat == EXT_ATTENTE && ext_bloque(&e, t0 + r + 499) && !ext_bloque(&e, t0 + r + 500) && !ext_coupe(&e),
            "verrou apres rallumage : etat %u", e.etat);
      /* appui tenu 5 s apres le rallumage : pas de nouvelle extinction (cause de R1) */
      for (uint32_t k = 0; k < 5000; k += PAS_LENT) { r += PAS_LENT; if (PAS(1) == EXT_DEBUT_EXTINCTION) ext2 = 1; }
      CHECK(!ext2 && e.etat == EXT_ATTENTE, "re-extinction sur appui tenu apres rallumage");
      for (uint32_t k = 0; k <= 200; k += PAS_LENT) { r += PAS_LENT; PAS(0); }
      CHECK(e.etat == EXT_MARCHE && !ext_bloque(&e, t0 + r), "pas rearme apres le rallumage");
#undef PAS
    }
}

/* calendrier : simule duree ms a 1 kHz, une trame au plus par ms, boites pleines une ms sur refus (0 = jamais) */
static void calendrier(uint32_t t0, uint32_t duree, uint32_t echo_des, uint32_t refus, unsigned n[AFF_NB + 1],
                       long premier[AFF_NB + 1], uint32_t *ecart_max)
{
    aff_cal_t c = {{0}, 0, 0, 0};
    aff_cal_init(&c, t0);
    long dernier[AFF_NB + 1];
    for (int i = 0; i <= AFF_NB; i++) { n[i] = 0; premier[i] = -1; dernier[i] = -1; ecart_max[i] = 0; }
    for (uint32_t t = 0; t <= duree; t++) {
        int i = aff_cal_due(&c, t0 + t, 0, 0, t >= echo_des);
        if (i < 0) continue;
        if (refus && t % refus == 0) continue;                     /* aucune boite libre : due au tour suivant */
        aff_cal_envoyee(&c, i, t0 + t);
        if (premier[i] < 0) premier[i] = (long)t;
        if (dernier[i] >= 0 && t - (uint32_t)dernier[i] > ecart_max[i]) ecart_max[i] = t - (uint32_t)dernier[i];
        dernier[i] = (long)t;
        n[i]++;
    }
    CHECK(c.vie == n[AFF_3000], "compteur de vie %u pour %u trames 0x3000", (unsigned)c.vie, n[AFF_3000]);
}

static void test_calendrier(uint32_t t0)
{
    static const uint16_t per[AFF_NB] = AFF_PERIODES, ph[AFF_NB] = AFF_PHASES;
    static const char *nom[AFF_NB] = {"3202", "3206", "3207", "3204", "1200", "3000"};
    unsigned n[AFF_NB + 1]; long premier[AFF_NB + 1]; uint32_t em[AFF_NB + 1];
    calendrier(t0, 100000, 0, 0, n, premier, em);
    for (int i = 0; i < AFF_NB; i++) {
        long attendu = (long)((100000u - ph[i]) / per[i]) + 1;
        printf("  0x%s : %u trames en 100 s (attendu %ld), premiere a %ld ms, ecart max %u ms\n", nom[i], n[i], attendu, premier[i], (unsigned)em[i]);
        CHECK((long)n[i] >= attendu - 1 && (long)n[i] <= attendu + 1, "0x%s : %u trames", nom[i], n[i]);
        CHECK(premier[i] >= ph[i] && premier[i] <= ph[i] + AFF_NB, "0x%s : premiere a %ld", nom[i], premier[i]);
        CHECK(em[i] >= per[i] && em[i] <= per[i] + (uint32_t)AFF_NB, "0x%s : ecart max %u", nom[i], (unsigned)em[i]);
    }
    /* 0x3206 seulement apres la premiere 0x6300 */
    calendrier(t0, 10000, 5000, 0, n, premier, em);
    CHECK(premier[AFF_3206] >= 5000 && premier[AFF_3206] <= 5002, "0x3206 avant la premiere 0x6300 (%ld)", premier[AFF_3206]);
    /* boites pleines une ms sur 3 : rien de perdu, seulement retarde */
    calendrier(t0, 100000, 0, 3, n, premier, em);
    for (int i = 0; i < AFF_NB; i++) {
        long attendu = (long)((100000u - ph[i]) / per[i]) + 1;
        CHECK((long)n[i] >= attendu - 1 && em[i] <= per[i] + 2u * (uint32_t)AFF_NB, "refus : 0x%s %u trames, ecart %u", nom[i], n[i], (unsigned)em[i]);
    }
    /* extinction : seulement 0x3204 = 00, toutes les 40 ms, 12 ou 13 en 500 ms ; eteint : rien */
    { aff_cal_t c = {{0}, 0, 0, 0}; aff_cal_init(&c, t0);
      for (uint32_t t = 0; t < 3000; t++) { int i = aff_cal_due(&c, t0 + t, 0, 0, 1); if (i >= 0) aff_cal_envoyee(&c, i, t0 + t); }
      uint32_t td = t0 + 3000; aff_cal_debut_extinction(&c);
      unsigned n00 = 0, autres = 0; long p00 = -1;
      for (uint32_t t = 0; t < EXT_TRAMES_00_MS; t++) {
          int i = aff_cal_due(&c, td + t, 1, 1, 1);
          if (i == AFF_3204_00) { n00++; if (p00 < 0) p00 = (long)t; aff_cal_envoyee(&c, i, td + t); } else if (i >= 0) autres++;
      }
      printf("  extinction : %u trames 0x3204 = 00 en 500 ms, premiere a %ld ms\n", n00, p00);
      CHECK((n00 == 12 || n00 == 13) && autres == 0 && p00 == 0, "extinction : %u trames 00, %u autres, premiere %ld", n00, autres, p00);
      CHECK(aff_id(AFF_3204_00) == 0x12F83204u, "id 3204 00 %08X", (unsigned)aff_id(AFF_3204_00));
      unsigned rien = 0;
      for (uint32_t t = 0; t < 20000; t++) if (aff_cal_due(&c, td + 500 + t, 1, 0, 1) >= 0) rien++;
      CHECK(rien == 0, "eteint : %u trames", rien);
      /* rallumage : calendrier relance a ses phases, compteur de vie conserve */
      uint32_t vie = c.vie, tr = td + 30000; aff_cal_init(&c, tr);
      int i0 = -1; uint32_t tp = 0;
      for (uint32_t t = 0; t < 200 && i0 < 0; t++) { i0 = aff_cal_due(&c, tr + t, 0, 0, 1); tp = t; }
      CHECK(i0 == AFF_3202 && tp == 100 && c.vie == vie, "rallumage : %d a %u ms, vie %u/%u", i0, (unsigned)tp, (unsigned)c.vie, (unsigned)vie); }
    /* identifiants */
    static const uint32_t ids[AFF_NB] = {0x12F83202u, 0x12F83206u, 0x12F83207u, 0x12F83204u, 0x12FF1200u, 0x12F83000u};
    for (int i = 0; i < AFF_NB; i++) CHECK(aff_id(i) == ids[i], "id %d : %08X", i, (unsigned)aff_id(i));
}

static void test_reception(uint32_t t0)
{
    aff_rx_t r = AFF_RX_INIT;
    CHECK(aff_rx_age(&r, t0 + 5) == 65535u && r.min_6303 == AFF_6303_JAMAIS_RECUE && !r.vu_6300, "etat initial");
    uint8_t d6300[4] = {0x15, 0x0D, 0, 1}, d6303[1] = {0xFF};
    aff_rx_note(&r, 0x6300, d6300, 4, t0 + 10);
    aff_rx_note(&r, 0x6303, d6303, 1, t0 + 20);
    aff_rx_note(&r, 0x1300, d6303, 1, t0 + 30);                  /* alarme de l'afficheur : comptee, rien d'autre */
    CHECK(r.n == 3 && r.code_6300 == 0x0D && r.vu_6300 && r.min_6303 == 0xFF, "releve : n %u code %02X min %02X", r.n, r.code_6300, r.min_6303);
    CHECK(aff_rx_age(&r, t0 + 130) == 100 && aff_rx_age(&r, t0 + 30 + 70000) == 65535u, "age %u", aff_rx_age(&r, t0 + 130));
    aff_rx_note(&r, 0x6300, d6300, 1, t0 + 40);                  /* DLC trop court : code inchange */
    CHECK(r.code_6300 == 0x0D && r.n == 4, "DLC 1");
    aff_rx_t q = AFF_RX_INIT; q.n = 65535u; aff_rx_note(&q, 0x6301, d6300, 0, t0); CHECK(q.n == 0 && q.vu, "reboucle du compteur");
}

static void test_hall(void)
{
    /* entree b0 PC6, b1 PC7, b2 PC8 ; sortie PC6 << 2 | PC7 | PC8 << 1 */
    for (uint32_t g = 0; g < 8; g++) {
        uint8_t pc6 = g & 1u, pc7 = (g >> 1) & 1u, pc8 = (g >> 2) & 1u;
        CHECK(aff_hall_aoci(g) == (uint8_t)((pc6 << 2) | pc7 | (pc8 << 1)), "Hall %u -> %u", (unsigned)g, aff_hall_aoci(g));
    }
    CHECK(aff_hall_aoci(0xF8u | 5u) == aff_hall_aoci(5u), "bits hauts ignores");
}

int main(void)
{
#ifdef ROUTE_BUILD
    printf("ROUTE_BUILD\n");
#endif
    /* deux origines de temps : apres le demarrage, et juste avant la reboucle de bench_ms (2^32 ms) */
    static const uint32_t origines[2] = {1000u, 0xFFFFFFFFu - 3000u};
    for (int k = 0; k < 2; k++) {
        printf("origine %u ms\n", (unsigned)origines[k]);
        test_extinction(origines[k]);
        test_calendrier(origines[k]);
        test_reception(origines[k]);
    }
    test_hall();
    printf("== %d verifications OK, %d ECHECS -> %s\n", n_ok, n_ko, n_ko ? "ROUGE" : "VERT");
    return n_ko ? 1 : 0;
}
