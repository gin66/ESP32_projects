#include <stdint.h>

#include "read.h"

void digitize(const uint8_t* rgb_565, uint8_t* digitized, struct read_s *read) {
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

  // Clear frame 8 bits
  for (uint16_t row = 0; row < HEIGHT;row++) {
	  digitized[row * WIDTH/8] = 0;
	  digitized[row * WIDTH/8 + WIDTH/8 - 1] = 0;
  }
  out = digitized;
  for (uint16_t row = 0; row < 7;row++) {
	  for (uint16_t col = 0;col < WIDTH/8;col++) {
		  *out++ = 0;
	  }
  }
  out = &digitized[WIDTH/8*(HEIGHT-8)];
  for (uint16_t row = 0; row < 7;row++) {
	  for (uint16_t col = 0;col < WIDTH/8;col++) {
		  *out++ = 0;
	  }
  }

  read->ok = 1;
}

static void filter(uint8_t* digitized, uint8_t *filtered) {
  uint8_t *out = filtered;
  for (uint16_t nbytes = WIDTH/8*HEIGHT; nbytes > 0;nbytes--) {
	  *out++ = 0;
  }

  uint8_t *in = &digitized[WIDTH/8];
  out = &filtered[WIDTH/8];
  for (uint16_t nbytes = WIDTH/8*(HEIGHT-2); nbytes > 0;nbytes--) {
	  uint8_t p = *in;
	  uint8_t p_left = in[-1];
	  uint8_t p_right = in[1];
	  uint8_t p_up = in[-WIDTH/8];
	  uint8_t p_down = in[WIDTH/8];
	  p_left = ((p_left & 1) << 7) | (p >> 1);
	  p_right = ((p_right & 0x80) >> 7) | (p << 1);
	  p = p_left & p_right & p_up & p_down;

	*out++ = p;
	in++;
  }
}

void find_pointer(uint8_t* digitized, uint8_t *filtered, uint8_t *temp, struct read_s *read) {
	filter(digitized, temp);
	filter(temp, filtered);
	filter(filtered, temp);
	filter(temp, filtered);
	filter(filtered, temp);
	filter(temp, filtered);
}
