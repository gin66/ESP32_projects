#pragma once

#include <stdint.h>

/* Modelled water-out temperature (S25).
 *
 * The installation has no return-line sensor. The first model is the measured
 * duty -> off/Soll table from the bench:
 *   100 -> 70 °C, 200 -> 60 °C, 300 -> 54 °C, 400 -> 44 °C, 500 -> 42 °C.
 * It is deliberately a strategy: callers must treat a false return as "no
 * value" and never substitute a guess.
 */
struct WaterModel {
  virtual ~WaterModel() = default;
  virtual bool estimate(int16_t water_in_c100, uint16_t duty_permille,
                        int16_t chamber_c100, int16_t* out_c100) const = 0;
  virtual const char* provenance() const = 0;
};

/* The installed model, or nullptr when none is installed. */
const WaterModel* water_model_default(void);
