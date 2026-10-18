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

  const char* provenance() const override { return "off table v1"; }
};

OffTableModel s_model;

}  // namespace

const WaterModel* water_model_default(void) { return &s_model; }
