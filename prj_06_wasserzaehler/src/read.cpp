#include <stdint.h>

#include "read.h"

void find_pointer(const uint8_t* rgb_565, uint8_t* digitized, struct read_s *read) {
  const uint8_t* pixel = rgb_565;
  uint8_t* out = digitized;
  for (uint32_t i = WIDTH * HEIGHT; i > 0; i -= 8) {
    uint8_t mask = 0;
    for (uint8_t j = 0; j < 8; j++) {
      uint8_t rgb1 = *pixel++;
      uint8_t rgb2 = *pixel++;
      // rrrrrggg.gggbbbbb
      uint8_t r = (rgb1 >> 2) & 0x3e;
      uint8_t g = (((rgb1 << 3) & 0x38) | ((rgb2 >> 5) & 0x07));
      uint8_t b = (rgb2 << 1) & 0x3e;
      mask <<= 1;
      uint16_t rx = r << 2;
      rx *= r;
      uint16_t gx = (g << 1) + g;
      gx *= g;
      uint16_t bx = (b << 1) + b;
      bx *= b;

      if ((rx > gx + bx) && (r > g) && (r > b)) {
        mask |= 1;
      }
    }
    *out++ = mask;
  }
  read->ok = 1;
}
