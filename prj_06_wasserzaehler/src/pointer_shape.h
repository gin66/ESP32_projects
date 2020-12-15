#include <stdint.h>

struct shape_s {
  int16_t y_min;
  int16_t y_max;
  int16_t x_min[40];
  int16_t x_max[40];
};

extern const struct shape_s shapes[20];
