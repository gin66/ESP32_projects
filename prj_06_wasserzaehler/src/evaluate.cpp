#include "evaluate.h"

#include <stdio.h>

void rtc_ram_buffer_init(struct rtc_ram_buffer_s *b) {
  b->windex = 0;
  b->rindex = 0;
}

#define ANG_RANGE 100
#define ANG_STEP 5

static uint8_t delta(uint8_t a1, uint8_t a2) {
  uint8_t da = 0;
  if (a1 > a2) {
    da = a1 - a2;
  }
  if (a1 < a2) {
    da = a2 - a1;
  }
  if (da > ANG_RANGE / 2) {
    da = ANG_RANGE - da;
  }
  return da;
}

int8_t rtc_ram_buffer_add(struct rtc_ram_buffer_s *b, uint32_t timestamp,
                          uint16_t angle0, uint16_t angle1, uint16_t angle2,
                          uint16_t angle3) {
  if (b->windex - NUM_ENTRIES == b->rindex) {
    b->rindex++;
  }
  struct entry_s *e = &b->entry[b->windex++ & NUM_ENTRIES_MASK];
  e->timestamp = timestamp;
  e->angle[0] = angle0 * ANG_STEP / 18;
  e->angle[1] = angle1 * ANG_STEP / 18;
  e->angle[2] = angle2 * ANG_STEP / 18;
  e->angle[3] = angle3 * ANG_STEP / 18;

  uint16_t num_data = b->windex - b->rindex;
  if (num_data < 5) {
    return -1;
  }

#define WINDOW_SIZE 20
  uint16_t window = num_data;
  if (window > WINDOW_SIZE) {
    window = WINDOW_SIZE;
  }
  uint8_t err[WINDOW_SIZE];
  for (uint16_t i = 0; i < window; i++) {
    err[i] = 0;
  }
  for (uint16_t i = 0; i < window - 1; i++) {
    uint16_t j1 = (b->windex + NUM_ENTRIES - window + i) & NUM_ENTRIES_MASK;
    uint16_t j2 = (b->windex + NUM_ENTRIES - window + i + 1) & NUM_ENTRIES_MASK;
    uint8_t a1 = b->entry[j1].angle[0];
    uint8_t a2 = b->entry[j2].angle[0];
    uint8_t da = delta(a1, a2);
    if (da > 2 * ANG_STEP) {
      err[i]++;
      err[i + 1]++;
    }
  }

  uint8_t ok = 0;
  uint16_t i_first = 0xffff;
  uint16_t i_last = 0xffff;
  for (uint16_t i = 0; i < window; i++) {
    if (err[i] == 0) {
      ok++;
      i_last = i;
      if (i_first == 0xffff) {
        i_first = i;
      }
    }
  }
  if (ok <= 1) {
    return -1;
  }

  uint16_t angle[WINDOW_SIZE];
  for (uint16_t i = 0; i < window; i++) {
    angle[i] = 0;
  }
  {
    uint16_t j1 =
        (b->windex + NUM_ENTRIES - window + i_first) & NUM_ENTRIES_MASK;
    angle[i_first] = b->entry[j1].angle[0];
    uint16_t last_angle = angle[i_first];
    for (uint16_t i = i_first + 1; i <= i_last; i++) {
      if (err[i] != 0) {
        continue;
      }
      uint16_t j = (b->windex + NUM_ENTRIES - window + i) & NUM_ENTRIES_MASK;
      uint16_t this_angle = b->entry[j].angle[0];
      while (this_angle + 2 * ANG_STEP < last_angle) {
        this_angle += ANG_RANGE;
      }
      angle[i] = this_angle;
      last_angle = this_angle;
    }
  }

  // Steigung in 1h
  {
    uint32_t s = 0;
    uint16_t j1 =
        (b->windex + NUM_ENTRIES - window + i_first) & NUM_ENTRIES_MASK;
    uint16_t j2 =
        (b->windex + NUM_ENTRIES - window + i_last) & NUM_ENTRIES_MASK;

    uint16_t a1 = angle[i_first];
    uint16_t a2 = angle[i_last];
    uint32_t t1 = b->entry[j1].timestamp;
    uint32_t t2 = b->entry[j2].timestamp;

    if (a2 > a1) {
      s = a2 - a1;
    }
    s *= 3600;
    s /= (t2 - t1);
    b->steigung = s;
  }

  if (err[window - 1] != 0) {
    return -1;
  }

  return 0;
}

uint16_t water_consumption(struct rtc_ram_buffer_s *b) {
  // return b->entry[(b->windex+NUM_ENTRIES-1) & NUM_ENTRIES_MASK].angle[0];
  return b->steigung;
}
