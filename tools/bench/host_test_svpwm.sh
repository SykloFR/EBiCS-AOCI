#!/bin/sh
# Test hote de svpwm() (porte 4, A1) : extrait la fonction de src/FOC.c sans la recopier, compile avec le gcc hote,
# balaie 36 000 angles a l'amplitude maximale. Usage depuis la racine du depot : sh tools/bench/host_test_svpwm.sh
set -e
cd "$(dirname "$0")/../.."
OUT=build-m3-banc/host
mkdir -p "$OUT"
sed -n '/^void svpwm(q31_t q31_u_alpha, q31_t q31_u_beta)[[:space:]]*{/,/^}/p' src/FOC.c > "$OUT/svpwm_extrait.c"
test -s "$OUT/svpwm_extrait.c"
SQRT3=$(grep -E '^#define[[:space:]]+_SQRT3' inc/FOC.h | awk '{print $3}')
gcc -O2 -Wall -Iinc -I"$OUT" -DBENCH_BUILD -DCURRENT_SIGN=-1 -D_SQRT3="$SQRT3" tools/bench/host_test_svpwm.c -lm -o "$OUT/host_test_svpwm.exe"
"$OUT/host_test_svpwm.exe"
