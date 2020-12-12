#include "read.h"

#include <stdint.h>
#include <stdio.h>

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

uint8_t min(uint8_t a, uint8_t b) { return (a > b) ? b : a; }
uint8_t max(uint8_t a, uint8_t b) { return (a > b) ? a : b; }

int8_t find_filled_bytes(uint8_t *bitimage, struct read_s *read) {
  uint8_t *p = bitimage;

  uint8_t last_row_areas = 0;
  struct area_s {
    uint8_t from;
    uint8_t to;
    uint8_t cnt;
  } last_areas[5];

  // one run revealed 43 filled bytes
  for (uint16_t row = 0; row < HEIGHT; row++) {
    uint8_t this_row_cnt = 0;
    struct area_s areas[5];
    uint8_t in_area = 0;
    for (uint8_t col = 0; col < WIDTH / 8; col++) {
      if (*p++ != 0) {
        if (in_area == 0) {
          in_area = 1;
          areas[this_row_cnt].from = col;
          areas[this_row_cnt].to = 0;
          areas[this_row_cnt++].cnt = 1;
        }
      } else {
        if (in_area == 1) {
          in_area = 0;
          areas[this_row_cnt - 1].to = col - 1;
        }
      }
      if (this_row_cnt == 5) {
        return -1;
      }
    }

    if ((this_row_cnt == 0) && (last_row_areas == 0)) {
      continue;
    }
    // printf("ROW %d: %d %d\n",row,last_row_areas,this_row_cnt);
    if (last_row_areas == 0) {
      last_row_areas = this_row_cnt;
      for (uint8_t i = 0; i < 5; i++) {
        last_areas[i] = areas[i];
      }
      continue;
    }

    uint8_t last_i = 0;
    uint8_t curr_i = 0;
    uint8_t new_i = 0;
    struct area_s new_areas[10];
    while ((last_i < last_row_areas) || (curr_i < this_row_cnt)) {
      uint8_t overlapping = 0;
      uint8_t area_finished = 0;
      if ((last_i < last_row_areas) && (curr_i < this_row_cnt)) {
        // perhaps overlapping
        if (last_areas[last_i].to < areas[curr_i].from) {
          area_finished = 1;
        } else if (last_areas[last_i].from <= areas[curr_i].to) {
          // overlapping
          overlapping = 1;
        }
      } else if (last_i < last_row_areas) {
        area_finished = 1;
      }
      if (overlapping) {
        new_areas[new_i].cnt = last_areas[last_i].cnt + 1;
        new_areas[new_i].from =
            min(last_areas[last_i].from, areas[curr_i].from);
        new_areas[new_i].to = max(last_areas[last_i].to, areas[curr_i].to);
        last_i++;
        curr_i++;
        new_i++;
      } else if (area_finished) {
        struct pointer_s *px = &read->pointer[read->candidates++];
        px->row_from = row - last_areas[last_i].cnt;
        px->row_to = row - 1;
        px->col_from = last_areas[last_i].from * 8;
        px->col_to = last_areas[last_i].to * 8 + 7;
        printf("found: height = %d\n", last_areas[last_i].cnt);
        last_i++;
      } else /* area_new */ {
        new_areas[new_i++] = areas[curr_i++];
      }
    }
    for (last_row_areas = 0; (last_row_areas < new_i) && (last_row_areas < 5);
         last_row_areas++) {
      last_areas[last_row_areas] = new_areas[last_row_areas];
    }
  }
  return 0;
}

void find_pointer(uint8_t *digitized, uint8_t *filtered, uint8_t *temp,
                  struct read_s *read) {
  filter(digitized, filtered);
  filter(filtered, temp);
  filter(temp, filtered);

  find_filled_bytes(filtered, read);
}
