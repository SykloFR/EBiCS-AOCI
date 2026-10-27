/* Frontieres Hall du sens de marche (theta decroissant) tirees de la ROTATION, porte 4, 27/10/2026.
   Ecrit par aoci-firmware-re/validation/porte-4/analyse-20261027/table_hall_rotation.py depuis les journaux
   velo-pepins-walk-5pts, velo-ccr-walk-5pts, velo-avantf4-walk-5pts, velo-largeur-walk-5pts.
   Positions relatives = durees des secteurs en rotation ; ancrage absolu = moyenne de la table A5
   (bench_hall_angles.h), puis decalage de -6.5 deg (ancrage propre au sens, 0 = sans).
   Remplace les Hall_xy A5 en build de banc quand ce fichier existe (main.c).
   Ne pas editer a la main. */
#ifndef BENCH_HALL_ROTATION_H_
#define BENCH_HALL_ROTATION_H_
#define BENCH_HALL_ROTATION 1
#define BENCH_HALL_ROT_64 (-503689866)   /* 317.8 deg (A5 : 325.5) */
#define BENCH_HALL_ROT_45 (188161960)   /* 15.8 deg (A5 : 20.0) */
#define BENCH_HALL_ROT_51 (930413340)   /* 78.0 deg (A5 : 79.5) */
#define BENCH_HALL_ROT_13 (1626494876)   /* 136.3 deg (A5 : 143.5) */
#define BENCH_HALL_ROT_32 (-1928423094)   /* 198.4 deg (A5 : 206.0) */
#define BENCH_HALL_ROT_26 (-1237534034)   /* 256.3 deg (A5 : 267.0) */
#endif
