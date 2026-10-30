#!/bin/sh
# Test hote de la mesure des courants en forte modulation (lot B3, 30/10/2026) : extrait svpwm() de src/FOC.c comme
# host_test_svpwm.sh, compile avec les macros du build route (U_MAX_BENCH, TRIGGER_OFFSET_ADC_DYN) et rend un verdict.
# Usage depuis la racine du depot : sh tools/bench/host_test_adc_dyn.sh
set -e
cd "$(dirname "$0")/../.."
OUT=build-m3-route/host
mkdir -p "$OUT"
sed -n '/^void svpwm(q31_t q31_u_alpha, q31_t q31_u_beta)[[:space:]]*{/,/^}/p' src/FOC.c > "$OUT/svpwm_extrait.c"
test -s "$OUT/svpwm_extrait.c"
SQRT3=$(grep -E '^#define[[:space:]]+_SQRT3' inc/FOC.h | awk '{print $3}')
gcc -O2 -Wall -Iinc -I"$OUT" -DBENCH_BUILD -DROUTE_BUILD -DCURRENT_SIGN=1 -D_SQRT3="$SQRT3" tools/bench/host_test_adc_dyn.c -lm -o "$OUT/host_test_adc_dyn.exe"
"$OUT/host_test_adc_dyn.exe"
