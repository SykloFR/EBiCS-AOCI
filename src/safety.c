/*
 * safety.c : voir safety.h. PROPOSITION POUR REVUE, non flashee.
 */
#include "safety.h"
#include "config.h"

volatile uint8_t safety_fault_code_latched = FAULT_NONE;

void safety_fault(uint8_t code)
{
    safety_pwm_off();                          /* d'abord couper, ensuite seulement memoriser */
    if (safety_fault_code_latched == FAULT_NONE) {
        safety_fault_code_latched = code;
    }
}

/* Surintensite de phase : seuil unique PH_CURRENT_TRIP tenu PH_TRIP_PERIODS periodes de suite (comme le palier 3 de l'origine),
   plus saturation de l'ampli de mesure (|i| >= 2000 points sur 2048 : coupure immediate). */
#define PH_SATURATION_COUNTS 2000

static inline int16_t abs16(int16_t v) { return (v < 0) ? (int16_t)-v : v; }

void safety_check_phase_currents(int16_t i1, int16_t i2, int16_t i3)
{
    static uint8_t trip_count = 0;
    int16_t a = abs16(i1), b = abs16(i2), c = abs16(i3);
    int16_t m = (a > b) ? a : b;
    if (c > m) m = c;

    if (m >= PH_SATURATION_COUNTS) {
        safety_fault(FAULT_OVERCURRENT);
        return;
    }
    if (m >= PH_CURRENT_TRIP) {
        if (++trip_count >= PH_TRIP_PERIODS) {
            safety_fault(FAULT_OVERCURRENT);
        }
    } else {
        trip_count = 0;
    }
}
