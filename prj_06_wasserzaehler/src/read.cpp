#include "read.h"

#include <stdint.h>
#include <stdio.h>

#include "pointer_shape.h"

void digitize(const uint8_t *bgr_888, uint8_t *digitized) {
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

void find_candidates(uint8_t *bitimage, struct read_s *read) {
  read->candidates = 0;
  uint8_t *p = bitimage;

  uint8_t last_row_areas = 0;
  struct area_s {
    uint8_t from;
    uint8_t to;
    uint8_t cnt;
  } last_areas[5];

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
        return;
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
        if (last_areas[last_i].cnt > 1) {
          struct pointer_s *px = &read->pointer[read->candidates];
          px->row_from = row - last_areas[last_i].cnt;
          px->row_to = row - 1;
          px->col_from = last_areas[last_i].from * 8;
          px->col_to = last_areas[last_i].to * 8 + 7;
          px->row_center2 = px->row_from + px->row_to;
          px->col_center2 = px->col_from + px->col_to;
          // printf("found: row = %d  height = %d  col=%d..%d\n", row-1
          // ,last_areas[last_i].cnt, px->col_from, px->col_to);
          if (read->candidates == 5) {  // already 6 candidates. delete smallest
            uint16_t smallest = HEIGHT;
            uint8_t min_i = 0;
            for (uint8_t i = 0; i <= 5; i++) {
              struct pointer_s *px = &read->pointer[i];
              if (px->row_to - px->row_from <= smallest) {
                smallest = px->row_to - px->row_from;
                min_i = i;
              }
            }
            read->pointer[min_i] = read->pointer[5];
          } else {
            read->candidates++;
          }
        }
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
}

void filter_by_geometry(struct read_s *read) {
  // Here are 5 candidates !
  //
  // Find most distant candidates.

  int32_t max_dist = 0;
  uint8_t c1 = 0;
  uint8_t c4 = 0;
  for (uint8_t i = 0; i < 4; i++) {
    int16_t r_i = read->pointer[i].row_center2;
    int16_t c_i = read->pointer[i].col_center2;
    for (uint8_t j = i + 1; j < 5; j++) {
      int16_t r_j = read->pointer[j].row_center2;
      int16_t c_j = read->pointer[j].col_center2;

      int16_t dr = r_i - r_j;
      int16_t dc = c_i - c_j;

      int32_t dr2 = dr;
      dr2 *= dr;
      int32_t dc4 = dc;
      dc4 *= dc;

      int32_t dist = dr2 + dc4;
      if (dist > max_dist) {
        max_dist = dist;
        c1 = i;
        c4 = j;
      }
    }
  }

  read->radius2 = max_dist / 36 / 4;

  int16_t dr_14 =
      (read->pointer[c4].row_center2 - read->pointer[c1].row_center2) * 3 / 8;
  int16_t dc_14 =
      (read->pointer[c4].col_center2 - read->pointer[c1].col_center2) * 3 / 8;

  // Find those two short distant candidates
  // The approach is, that the line from the outter pointers is parallel to the
  // line between inner pointers And that inner line is approx. 1/3 of the
  // outter
  int32_t min_dist = 0x7fffffff;
  uint8_t c2 = 0;
  uint8_t c3 = 0;
  for (uint8_t i = 0; i < 5; i++) {
    if ((i == c1) || (i == c4)) {
      continue;
    }
    int16_t r_i = read->pointer[i].row_center2;
    int16_t c_i = read->pointer[i].col_center2;
    for (uint8_t j = 0; j < 5; j++) {
      if ((j == c1) || (j == c4) || (i == j)) {
        continue;
      }
      int16_t r_j = read->pointer[j].row_center2;
      int16_t c_j = read->pointer[j].col_center2;

      int16_t dr = r_j - r_i - dr_14;
      int16_t dc = c_j - c_i - dc_14;

      int32_t dr2 = dr;
      dr2 *= dr;
      int32_t dc4 = dc;
      dc4 *= dc;

      int32_t dist = dr2 + dc4;
      if (dist < min_dist) {
        min_dist = dist;
        c2 = i;
        c3 = j;
      }
    }
  }
  if (min_dist > read->radius2 * read->radius2 / 8) {
    // Distance too big
    // printf("TOO FAR AWAY: %d %d\n", min_dist, read->radius2/2);
    return;
  }
  // printf("%d %d\n", min_dist, read->radius2);

  // Check the distance of the remaining pointer to the outter.
  // The lower distance indicates the 0.0001m³ pointer
  for (uint8_t i = 0; i < 5; i++) {
    if ((i == c1) || (i == c2) || (i == c3) || (i == c4)) {
      continue;
    }
    // This should be triangle wheel
    int16_t r_i = read->pointer[i].row_center2;
    int16_t c_i = read->pointer[i].col_center2;

    int16_t r_1 = read->pointer[c1].row_center2;
    int16_t c_1 = read->pointer[c1].col_center2;
    int16_t r_4 = read->pointer[c4].row_center2;
    int16_t c_4 = read->pointer[c4].col_center2;

    int16_t dr1 = r_1 - r_i;
    int16_t dc1 = c_1 - c_i;
    int16_t dr4 = r_4 - r_i;
    int16_t dc4 = c_4 - c_i;

    int32_t dr1_2 = dr1;
    dr1_2 *= dr1;
    int32_t dc1_2 = dc1;
    dc1_2 *= dc1;
    int32_t ds1_2 = dr1_2 + dc1_2;

    int32_t dr4_2 = dr4;
    dr4_2 *= dr4;
    int32_t dc4_2 = dc4;
    dc4_2 *= dc4;
    int32_t ds4_2 = dr4_2 + dc4_2;

    if (ds1_2 < ds4_2) {
      // c1 is the 0.0001m³ pointer. So swap
      uint8_t tmp = c_1;
      c4 = c1;
      c1 = tmp;
      tmp = c2;
      c2 = c3;
      c3 = tmp;
    }

    // Now c1 is 0.1, c2 0.01, c3 0.001 and c4 0.0001
  }

  struct pointer_s p_new[4];
  p_new[0] = read->pointer[c1];
  p_new[1] = read->pointer[c2];
  p_new[2] = read->pointer[c3];
  p_new[3] = read->pointer[c4];

  read->pointer[0] = p_new[0];
  read->pointer[1] = p_new[1];
  read->pointer[2] = p_new[2];
  read->pointer[3] = p_new[3];
  read->candidates = 4;

  // printf("%d %d %d %d\n",c1,c2,c3,c4);
}

void update_center(const uint8_t *digitized, struct pointer_s *px,
                   int16_t radius2) {
  int16_t row_center = px->row_center2 / 2;
  int16_t col_center = px->col_center2 / 2;

  uint16_t points = 0;
  int32_t r_sum = 0;
  int32_t c_sum = 0;
  for (int16_t dr = -50; dr < 50; dr++) {
    int16_t drr = dr * dr;
    if (drr > radius2) {
      continue;
    }
    for (int16_t dc = -50; dc < 50; dc++) {
      if (drr + dc * dc > radius2) {
        continue;
      }

      uint16_t row = row_center + dr;
      uint16_t col = col_center + dc;
      uint16_t index = row * WIDTH / 8;
      index += col >> 3;
      uint8_t mask = 0x80 >> (col & 0x07);

      if ((digitized[index] & mask) != 0) {
        points++;
        r_sum += dr;
        c_sum += dc;
      }
    }
  }
  if (points == 0) {
    return;
  }
  px->row_center2 += r_sum * 2 / points;
  px->col_center2 += c_sum * 2 / points;
}

int16_t calc_match(const uint8_t *digitized, const struct pointer_s *px,
                   int16_t radius2, uint8_t angle) {
  const struct shape_s *shape = &shapes[angle];

  int16_t row_center = px->row_center2 / 2;
  int16_t col_center = px->col_center2 / 2;

  int16_t in_ok_points = 0;
  int16_t in_miss_points = 0;
  int16_t out_wrong_points = 0;
  for (int16_t dr = -40; dr < 40; dr++) {
    int16_t drr = dr * dr;
    if (drr > radius2) {
      continue;
    }
    for (int16_t dc = -40; dc < 40; dc++) {
      int16_t dcc = dc * dc;
      if (dcc > radius2) {
        continue;
      }
      if (drr + dcc > radius2) {
        continue;
      }

      uint16_t row = row_center + dr;
      uint16_t col = col_center + dc;
      uint16_t index = row * WIDTH / 8;
      index += col >> 3;
      uint8_t mask = 0x80 >> (col & 0x07);

      uint8_t expect = 0;
      if ((dr >= shape->y_min) && (dr <= shape->y_max)) {
        int8_t j = dr - shape->y_min;
        int16_t x_min = shape->x_min[j];
        int16_t x_max = shape->x_max[j];
        if ((dc >= x_min) && (dc <= x_max)) {
          expect = 1;
        }
      }

      if ((digitized[index] & mask) != 0) {
        if (expect == 1) {
          in_ok_points++;
        } else {
          out_wrong_points++;
        }
      } else {
        if (expect == 1) {
          in_miss_points++;
        }
      }
    }
  }
  int16_t res = 4 * in_ok_points - 2 * out_wrong_points - in_miss_points;
  // printf("%d  inside shape: ok=%d/miss=%d  outside=%d => %d\n", angle,
  // in_ok_points, in_miss_points, out_wrong_points, res);
  return res;
}

void find_direction(const uint8_t *digitized, struct pointer_s *px,
                    int16_t radius2) {
  uint8_t best_angle = 0;
  int32_t max_mom = 0;
  for (uint8_t angle = 0; angle < 20; angle++) {
    int32_t mom = calc_match(digitized, px, radius2, angle);
    if (mom > max_mom) {
      max_mom = mom;
      best_angle = angle;
    }
  }
  px->angle = best_angle * 18;
  // printf("=> %d\n", best_angle);
}

void find_pointer(uint8_t *digitized, uint8_t *filtered, uint8_t *temp,
                  struct read_s *read) {
  filter(digitized, filtered);
  filter(filtered, temp);
  filter(temp, filtered);

  find_candidates(filtered, read);

  // printf("candidates: %d\n", read->candidates);
  if (read->candidates != 5) {
    read->candidates = 0;
    return;
  }
  filter_by_geometry(read);
  if (read->candidates == 5) {
    read->candidates = 0;
    return;
  }

  for (uint8_t i = 0; i < 4; i++) {
    struct pointer_s *px = &read->pointer[i];
    update_center(digitized, px, read->radius2);
    find_direction(digitized, px, read->radius2);
  }
}
