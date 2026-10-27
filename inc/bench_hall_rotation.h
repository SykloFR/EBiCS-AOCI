/* Frontieres Hall du sens de marche (theta decroissant) tirees de la ROTATION, porte 4, 27/10/2026.
   Ecrit par aoci-firmware-re/validation/porte-4/analyse-20261027/table_hall_rotation.py depuis les journaux
   velo-pepins-walk-5pts, velo-ccr-walk-5pts, velo-avantf4-walk-5pts, velo-largeur-walk-5pts.
   Positions relatives = durees des secteurs en rotation ; ancrage absolu = moyenne de la table A5
   (bench_hall_angles.h). Remplace les Hall_xy A5 en build de banc quand ce fichier existe (main.c).
   Ne pas editer a la main. */
#ifndef BENCH_HALL_ROTATION_H_
#define BENCH_HALL_ROTATION_H_
#define BENCH_HALL_ROTATION 1
#define BENCH_HALL_ROT_64 (-426141846)   /* 324.3 deg (A5 : 325.5) */
#define BENCH_HALL_ROT_45 (265709981)   /* 22.3 deg (A5 : 20.0) */
#define BENCH_HALL_ROT_51 (1007961361)   /* 84.5 deg (A5 : 79.5) */
#define BENCH_HALL_ROT_13 (1704042896)   /* 142.8 deg (A5 : 143.5) */
#define BENCH_HALL_ROT_32 (-1850875073)   /* 204.9 deg (A5 : 206.0) */
#define BENCH_HALL_ROT_26 (-1159986014)   /* 262.8 deg (A5 : 267.0) */
#endif
