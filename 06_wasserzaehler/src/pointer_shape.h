#include <stdint.h>

#define ANGLES 20

struct shape_s {
  int16_t y_min;
  int16_t y_max;
  int16_t x_min[121];
  int16_t x_max[121];
};

extern const struct shape_s shapes[20];
