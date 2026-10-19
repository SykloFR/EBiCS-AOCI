# Revue de la partie puissance du portage EBiCS vers le contrôleur AOCI

Dossier préparé le 16/10/2026 pour la revue croisée (Fangyan, systèmes embarqués, électronique de puissance et commande moteur). Il décrit **ce que le portage fait aujourd'hui sur l'étage de puissance, ce qui est faux ou à corriger sur ce matériel, ce qui est proposé en remplacement, et pourquoi**, avec les essais déjà faits sur la vraie carte.

**Rien de ce dossier n'a été validé sur un moteur sous tension. Rien n'a été flashé.** Les propositions sont un brouillon à relire, corriger ou refuser.

## Ce que le développeur doit faire

1. Lire `PROPOSITIONS.md` (12 points, chacun avec constat, risque, proposition, justification, essais faits, essais à faire).
2. Relire le code proposé : `puissance-proposee-src.diff` et `puissance-proposee-inc.diff` (différences exactes avec le code courant), ou directement `proposed/` (arbre complet).
3. Répondre aux questions ouvertes en fin de `PROPOSITIONS.md` (topologie de mesure du courant, polarité, échelle).
4. Faire les essais de banc listés (porte 3 sans puissance, puis porte 4 basse puissance).
5. Remplir `COMPTE-RENDU-modele.md` (un exemplaire par session de revue) et le ranger dans ce dossier.

## Contenu

| Fichier / dossier | Rôle |
|---|---|
| `PROPOSITIONS.md` | Document principal : 12 propositions justifiées, questions ouvertes, essais |
| `COMPTE-RENDU-modele.md` | Modèle de compte rendu à remplir par le développeur |
| `puissance-proposee-src.diff`, `puissance-proposee-inc.diff` | Écarts entre le code courant (`src/`, `inc/`) et la proposition |
| `proposed/` | Arbre de sources de la proposition (compile, ne doit pas être flashé) |
| `../tools/bench/` | Scripts d'essai sur la carte (lecture seule + RAM) et sur PC |
| `../../aoci-firmware-re/validation/porte-3/` | Sorties brutes de tous les essais du 16/10/2026 |

## Compiler

```
mingw32-make CORE=m3 aoci            # code courant : image build-m3/ebics_aoci_full.bin
mingw32-make CORE=m3 PROP=1 aoci     # proposition   : build-m3-prop/ebics_REVUE_NE_PAS_FLASHER.bin
```

Le code courant affiche un `#warning` à chaque compilation : ses constantes de puissance sont encore celles du M820. La proposition est volontairement nommée `..._REVUE_NE_PAS_FLASHER.bin`.

## Ce qui est déjà vérifié sur la carte (lecture seule et RAM, sans écrire en flash)

Matériel : contrôleur de banc n° 1 (APM32F103RC, firmware AOCI 36 V 15 A d'origine), alimenté par le WCH-Link seul (pas de batterie, étage de puissance non alimenté, `TIM1 MOE = 0` contrôlé avant chaque essai, abandon automatique sinon). La carte est remise en état d'origine par un reset après chaque essai ; la flash complète a été relue à la fin et est **identique octet pour octet** au dump de la porte 1.

| Essai | Résultat |
|---|---|
| Horloge réelle (`SystemInit` du portage exécuté en RAM) | 66 MHz verrouillés ; 132 000 009 cycles comptés en 2,001 s, soit 65,96 MHz ; `FLASH_ACR = 0x32` comme l'origine |
| Repli HSI (quartz simulé absent) | 64 MHz, CAN prescaler 8, débit mesuré 249,9 kbit/s |
| CAN en boucle interne silencieuse (rien sur le bus) | Identifiants AOCI `0x12F83200/01` émis et reçus, données exactes, débit mesuré **250,0 kbit/s** |
| Bases de temps TIMER1 / TIMER2 | 4000,0 Hz et 500,0 kHz (66 et 64 MHz) |
| ADC (lecture de toutes les voies à vide) | voir `PROPOSITIONS.md` P8 et P9 |
| Coût du PI et de la racine carrée | **flottant : 2901 cycles au pire (73 % du budget de 4000) ; entier : 479 cycles (12 %)** |
| Équivalence PI entier / flottant (PC) | écart maximal 2 points sur 4 millions de pas ; racine carrée exacte |
| Arbre d'appels de l'interruption rapide (analyse du binaire) | code courant : appels flottants et 64 bits ; proposition : aucun |

## Ce qui n'est PAS vérifié

La boucle de courant, la commutation, le temps mort réel, la fenêtre d'échantillonnage, la polarité et l'échelle des capteurs de courant, les seuils de coupure, le comportement en défaut, l'apprentissage des Hall. Tout cela demande l'étage de puissance sous tension et un oscilloscope : c'est l'objet de la revue.
