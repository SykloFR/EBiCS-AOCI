# Compte rendu de revue : partie puissance du portage EBiCS vers AOCI

Modèle à copier sous `revue-puissance/CR-AAAAMMJJ-<initiales>.md` et à remplir (une copie par session de revue).

## 1. Identification

| | |
|---|---|
| Développeur | |
| Date(s) de la revue | |
| Version relue | dossier `revue-puissance/` du ........ (date), commit / copie : ........ |
| Matériel utilisé | carte de banc n° ... ; alim de labo (modèle, réglage tension / courant) ; oscilloscope (modèle, sondes) ; analyseur logique ; autre |
| Durée passée | |

## 2. Périmètre effectivement relu

Cocher ce qui a été relu et testé (R = relu, T = testé au banc, N = non traité).

| Point | R | T | N | Remarque |
|---|---|---|---|---|
| P1 Constantes de temps et d'échantillonnage | | | | |
| P2 Coupure de surintensité | | | | |
| P3 Handlers de faute | | | | |
| P4 PI et racine en entiers | | | | |
| P5 Chien de garde | | | | |
| P6 Attentes au démarrage | | | | |
| P7 Protection matérielle | | | | |
| P8 Offsets de courant | | | | |
| P9 Températures | | | | |
| P10 État des sorties, temps mort | | | | |
| P11 Échelles de courant, gains du PI | | | | |
| P12 Apprentissage des Hall | | | | |
| Points non traités du dossier | | | | |

## 3. Avis point par point

Un bloc par point. Décision : **Accepté** / **Accepté avec modification** / **Refusé** / **À reprendre**.

### Pn : titre

- Décision :
- Justification (technique, mesure, calcul) :
- Modification apportée au code (fichier, résumé, diff joint) :
- Essai réalisé (montage, condition, résultat, capture d'écran ou fichier) :
- Point residuel :

## 4. Réponses aux questions ouvertes

| # | Question | Réponse | Preuve (mesure, capture, calcul) |
|---|---|---|---|
| 1 | Mesure du courant : shunts ou Rds(on) ? | | |
| 2 | Polarité de PA2, PA3, PA5, PA0 | | |
| 3 | Points par ampère (phase, batterie) | | |
| 4 | Instants d'échantillonnage, nombre de déclenchements par période | | |
| 5 | R, L, FCEM du moteur AOCI | | |
| 6 | Seuils de coupure retenus (un seul seuil ou trois paliers), protection sonde moteur, autotests | | |
| 7 | Politique de redémarrage après défaut / reset chien de garde | | |
| 8 | Ordre des phases, sens, correspondance Hall | | |

## 5. Mesures relevées (à joindre)

| Grandeur | Valeur mesurée | Attendu | Condition | Fichier |
|---|---|---|---|---|
| Fréquence PWM | | 16,5 kHz | | |
| Temps mort réel haut / bas | | ~380 ns + driver | | |
| Chevauchement haut / bas | | aucun | | |
| PWM à zéro au reset | | oui | | |
| Durée de l'interruption ADC (broche de test) | | < 4000 cycles (60,6 µs) | | |
| Nombre de déclenchements ADC par période | | 1 | | |
| Offsets de courant (3 phases + batterie) | | 2048 ± 128 / 1600..2300 | carte alimentée | |
| Points par ampère de phase | | ~3,14 (R = 1,5) | | |
| Temps entre dépassement de seuil et MOE coupé | | < 1 ms | | |
| Temps de coupure sur HardFault provoqué | | < 100 µs | | |

## 6. Modifications retenues

Liste des changements à intégrer au code principal (`src/`, `inc/`), avec la raison de chacun. Joindre le diff final.

## 7. Points ouverts

## 8. Recommandation pour la suite

- Porte 3 (sans puissance) : **franchie / non franchie**, motif :
- Conditions pour la porte 4 (24 V, 1 A) :
- Conditions pour la porte 5 (nominal) :
- Essais supplémentaires demandés :

## 9. Signature

Nom, date.
