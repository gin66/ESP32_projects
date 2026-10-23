#include "haus_water_model.h"

#include <stddef.h>

namespace {

class OffTableModel final : public WaterModel {
 public:
  bool estimate(int16_t water_in_c100, uint16_t duty_permille,
                int16_t chamber_c100, int16_t* out_c100) const override {
    (void)water_in_c100;
    (void)chamber_c100;
    if (!out_c100) return false;

    /* Measured off/Soll table. Outside the measured duty range the model has
     * nothing to say; the caller renders "--". */
    static const uint16_t duty[] = {100, 200, 300, 400, 500};
    static const int16_t temp[] = {7000, 6000, 5400, 4400, 4200};
    constexpr size_t kCount = sizeof(duty) / sizeof(duty[0]);
    if (duty_permille < duty[0] || duty_permille > duty[kCount - 1])
      return false;

    for (size_t i = 0; i + 1 < kCount; i++) {
      if (duty_permille <= duty[i + 1]) {
        const int32_t span = (int32_t)temp[i + 1] - temp[i];
        *out_c100 =
            (int16_t)(temp[i] + span * (int32_t)(duty_permille - duty[i]) /
                                    (int32_t)(duty[i + 1] - duty[i]));
        return true;
      }
    }
    *out_c100 = temp[kCount - 1];
    return true;
  }

  bool target_from_duty(uint16_t duty_permille,
                        int16_t* target_c100) const override {
    if (!target_c100) return false;

    /* Inverse of the off/Soll table. Given a target temperature, compute the
     * duty that produces it. Returns false for temperatures outside the valid
     * range (42…70 °C), for duty 0 (manual-only), and for duty 1000 (OFF,
     * which bypasses the model entirely). */
    static const uint16_t duty[] = {100, 200, 300, 400, 500};
    static const int16_t temp[] = {7000, 6000, 5400, 4400, 4200};
    constexpr size_t kCount = sizeof(duty) / sizeof(duty[0]);

    /* Duty 0 stays manual-only. Duty 1000 is real OFF — bypasses the inverse. */
    if (duty_permille == 0 || duty_permille == 1000) return false;

    /* Outside the table domain: 42 °C is the floor, 70 °C the ceiling. */
    if (duty_permille > duty[kCount - 1]) {
      /* Above duty 500: floor at 42 °C. */
      *target_c100 = temp[kCount - 1];
      return true;
    }
    if (duty_permille < duty[0]) {
      /* Below duty 100: ceiling at 70 °C. */
      *target_c100 = temp[0];
      return true;
    }

    /* Inverse interpolation: find the two knots surrounding this duty, then
     * compute the temperature fraction and apply it to the temperature range.
     *
     * Example: duty 283 → between knots (200, 6000) and (300, 5400).
     *   temp_fraction = (283 - 200) / (300 - 200) = 83/100 = 0.83
     *   target = 6000 + (5400 - 6000) * 0.83 = 6000 + (-600) * 83/100
     *          = 6000 - 498 = 5502 → rounds to 5500 (55 °C). */
    for (size_t i = 0; i + 1 < kCount; i++) {
      if (duty_permille <= duty[i + 1]) {
        const int32_t temp_span = (int32_t)temp[i + 1] - temp[i];
        const int32_t duty_span = (int32_t)duty[i + 1] - duty[i];
        const int32_t duty_offset = (int32_t)duty_permille - duty[i];
        *target_c100 = (int16_t)(temp[i] +
                                 temp_span * duty_offset / duty_span);
        return true;
      }
    }
    *target_c100 = temp[kCount - 1];
    return true;
  }

  const char* provenance() const override { return "off table v1"; }
};

OffTableModel s_model;

}  // namespace

const WaterModel* water_model_default(void) { return &s_model; }
