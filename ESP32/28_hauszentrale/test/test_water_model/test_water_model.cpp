#include <cassert>
#include <cstdio>

#include "haus_water_model.h"

int main() {
  const WaterModel* m = water_model_default();
  assert(m != nullptr);
  int16_t out = 0;

  assert(m->estimate(0, 100, 0, &out) && out == 7000);
  assert(m->estimate(0, 200, 0, &out) && out == 6000);
  assert(m->estimate(0, 300, 0, &out) && out == 5400);
  assert(m->estimate(0, 400, 0, &out) && out == 4400);
  assert(m->estimate(0, 500, 0, &out) && out == 4200);

  assert(m->estimate(0, 250, 0, &out) && out == 5700);
  assert(m->estimate(0, 450, 0, &out) && out == 4300);

  assert(!m->estimate(0, 99, 0, &out));
  assert(!m->estimate(0, 501, 0, &out));
  assert(!m->estimate(0, 1000, 0, &out));
  assert(!m->estimate(0, 300, 0, nullptr));

  std::printf("test_water_model: ok\n");
  return 0;
}
