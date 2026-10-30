#!/bin/sh
# Test hote de la mesure en mode dynamique avec le retard CC4 / CCR (30/10/2026). Usage : sh tools/bench/host_test_adc_dyn_retard.sh
set -e
cd "$(dirname "$0")/../.."
OUT=build-m3-route/host
mkdir -p "$OUT"
sed -n '/^void svpwm(q31_t q31_u_alpha, q31_t q31_u_beta)[[:space:]]*{/,/^}/p' src/FOC.c > "$OUT/svpwm_extrait.c"
SQRT3=$(grep -E '^#define[[:space:]]+_SQRT3' inc/FOC.h | awk '{print $3}')
gcc -O2 -Wall -Iinc -I"$OUT" -DBENCH_BUILD -DROUTE_BUILD -DCURRENT_SIGN=1 -D_SQRT3="$SQRT3" tools/bench/host_test_adc_dyn_retard.c -lm -o "$OUT/host_test_adc_dyn_retard.exe"
"$OUT/host_test_adc_dyn_retard.exe"
