# AGENTS.md - Règles de travail (projet ebics-aoci)

Portage du firmware EBiCS (Bafang M820, GD32F303, Cortex-M4F) vers le contrôleur AOCI
(APM32F103RC, Cortex-M3 sans FPU). Journal : `PORTAGE-AOCI.md`. Methode et portes :
`../aoci-firmware-re/methode-validation-avant-firmware.md`.

## Roles

- **Agent manager** : diagnostic, decisions, priorisation, revue des livrables. Ne descend pas dans
  l'execution. Produit des taches atomiques avec un critere d'acceptation mesurable.
- **Agent executant** : code, builds, tests, desassemblage, scripts, docs, diffs. Renvoie du mesure
  (fichiers, hash, resultats), jamais du raisonnement.

Un seul agent ecrit dans un fichier donne a un instant donne. Avant de commencer, verifier
(`git status`) qu'aucune autre session n'edite le meme fichier.

## Perimetre

- **Flash de la carte : seulement sur accord explicite** ; c'est reversible par SWD (voir porte 2).
- **Partie puissance** (`_T`, temps mort, ADC injecte, surintensite, BKIN, HardFault, etalonnage
  courant) : a traiter comme du code normal, avec tests mesures. Ces taches se font et se mesurent
  comme les autres ; presenter les valeurs comme provisoires tant qu'elles ne sont pas mesurees.
- Ne jamais lire `.env*`, `.mcp.json`, `settings.local.json`, `~/.claude*`, `~/.ssh`.
- Ne jamais contourner un outil manquant par un script maison (curl, SSH, WP-CLI).

## Edition de code

- **Outil Edit uniquement** (remplacement exact de chaine). **Interdit** : `Set-Content`,
  remplacement par regex PowerShell, reecriture globale de fichier.
- Encodage **UTF-8 sans BOM** ; preserver les fins de ligne existantes (CRLF).
- Ne jamais inserer de `\n` litteral dans une chaine (piege PowerShell). Apres edition, verifier
  la zone modifiee (`grep`) et l'integrite du fichier.
- Une modification = un fichier = un objectif. Pas de derive "pendant que j'y suis".

## Build et verification

- Cible AOCI : `mingw32-make CORE=m3 aoci`
- Non-regression M4 d'origine : `mingw32-make`
- Proposition puissance : `mingw32-make CORE=m3 PROP=1 aoci`
- Nettoyage : `mingw32-make CORE=m3 clean`
- Suivi des dependances `-MMD` actif : une modification d'en-tete recompile.
- **Regle de preuve** : ne jamais affirmer "ca compile" ou "c'est fait" sans build propre, hash, et
  presence du symbole/constante attendu. Reference : le bug du build creux (`-Os` + drapeau non
  `volatile` transformait `main` en boucle infinie, binaire vide). Un binaire qui compile peut etre vide.
- Toujours controler : `arm-none-eabi-size`, `arm-none-eabi-nm`, et le CRC via
  `python tools/make_aoci_image.py`.

## Tests

- Bench carte (`tools/bench/`) : `t1_flash_identique`, `t2_ram_clock_adc`, `t3_ram_can_loopback`
  (`--repli` pour le repli HSI), `t4_ram_timers`, `t5_ram_cost`. Necessitent pyocd + carte connectee.
- Test hote : `tools/bench/host_test_foc_int.c` (gcc hote) : equivalence PI entier / flottant.
- Apres toute modification du coeur : relancer le bench et comparer aux references :
  66 MHz (65,96 mesure), 250 kbit/s CAN, TIMER1 4000 Hz, TIMER2 500 kHz, PI entier ~479 cycles,
  PI flottant ~2901 cycles (budget 4000 a 16,5 kHz).

## Qualite : pas de surqualite

- Faire exactement ce qui est demande. Ne pas ajouter de refactor, d'optimisation, de fonctionnalite
  ou de documentation non demandes ("pendant que j'y suis" interdit).
- Si une tache peut s'etendre, s'arreter et le suggerer au lieu de continuer.
- Preferer une suggestion claire a une longue session qui tourne en rond.

## Format de sortie

- Concis. Pas de dump de raisonnement, pas de repetition du contexte.
- Compte-rendu : fichiers touches, commande(s) lancee(s), resultat mesure. Rien de plus.

## Fin de session (obligatoire)

Terminer chaque session par un bloc court, dans cet ordre :

1. **Fait** : fichiers touches + resultat mesure.
2. **Non fait** : ce qui reste, en liste.
3. **Suggestion** : une seule prochaine action proposee, avec une question oui/non
   ("Veux-tu qu'on fasse X ?").

Pas de budget a respecter : la regle est de ne pas tourner en rond et de rendre la main avec une
suggestion, pas de remplir la session.

## Handoff

Modele : `HANDOFF-TEMPLATE.md`. Taches atomiques, critere d'acceptation, perimetre ferme.

## Git

- Branche : `syklo-aoci`. Interdits : `push --force`, `reset --hard`, `stash`.
- `git add` fichier par fichier (jamais `-A`, `--all`, `.`). Commits atomiques.
- Ne pas committer : `build*/`, `.env*`, `.mcp.json`, artefacts de test (`tools/bench/**/*.bin`,
  `*.elf`, `*.map`, `*.exe`), dumps de flash.
