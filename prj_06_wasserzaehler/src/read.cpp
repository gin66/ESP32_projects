#include <stdint.h>

#include "read.h"

void digitize(const uint8_t *bgr_888, uint8_t *digitized, struct read_s *read) {
  uint8_t *out = digitized;
  for (uint32_t i = WIDTH * HEIGHT; i > 0; i -= 8) {
    uint8_t mask = 0;
    for (uint8_t j = 0; j < 8; j++) {
      mask <<= 1;
      uint8_t b = *bgr_888++;
      uint8_t g = *bgr_888++;
      uint8_t r = *bgr_888++;
      uint16_t rx = r;
      rx *= r;
      uint16_t gx = g;
      gx *= g;
      uint16_t bx = b;
      bx *= b;

      // 4*r*r > 3*(g*g+b*b)
      uint16_t gx_bx = (gx >> 1) + (bx >> 1);
      rx >>= 2;
      gx_bx >>= 2;
      gx_bx += gx_bx >> 1;  // *3/2

      if ((rx > gx_bx) && (r / 3 > g / 2) && (r / 3 > b / 2)) {
        mask |= 1;
      }
    }
    *out++ = mask;
  }

  // Clear frame 8 bits
  for (uint16_t row = 0; row < HEIGHT; row++) {
    digitized[row * WIDTH / 8] = 0;
    digitized[row * WIDTH / 8 + WIDTH / 8 - 1] = 0;
  }
  out = digitized;
  for (uint16_t row = 0; row < 8; row++) {
    for (uint16_t col = 0; col < WIDTH / 8; col++) {
      *out++ = 0;
    }
  }
  out = &digitized[WIDTH / 8 * (HEIGHT - 8)];
  for (uint16_t row = 0; row < 8; row++) {
    for (uint16_t col = 0; col < WIDTH / 8; col++) {
      *out++ = 0;
    }
  }

  read->ok = 1;
}

static void filter(uint8_t *digitized, uint8_t *filtered) {
  uint8_t *out = filtered;
  for (uint16_t nbytes = WIDTH / 8 * HEIGHT; nbytes > 0; nbytes--) {
    *out++ = 0;
  }

  uint8_t *in = &digitized[WIDTH / 8];
  out = &filtered[WIDTH / 8];
  for (uint16_t nbytes = WIDTH / 8 * (HEIGHT - 2); nbytes > 0; nbytes--) {
    uint8_t p = *in;
    uint8_t p_left = in[-1];
    uint8_t p_right = in[1];
    uint8_t p_up = in[-WIDTH / 8];
    uint8_t p_down = in[WIDTH / 8];
    p_left = ((p_left & 1) << 7) | (p >> 1);
    p_right = ((p_right & 0x80) >> 7) | (p << 1);
    p = p_left & p_right & p_up & p_down;

    *out++ = p;
    in++;
  }
}

void find_pointer(uint8_t *digitized, uint8_t *filtered, uint8_t *temp,
                  struct read_s *read) {
  filter(digitized, filtered);
  // filter(temp, filtered);
  // filter(filtered, temp);
  // filter(temp, filtered);
  filter(filtered, temp);
  filter(temp, filtered);
}
