#include "evaluate.h"

#include <stdio.h>

#ifdef ARDUINO_ARCH_ESP32
#include <esp32-hal-psram.h>
#else
#include <stdlib.h>
#define ps_malloc malloc
#endif

#if PSRAM_BUFFER_SIZE < (NUM_ENTRIES + 2) * 32
#error BUFFER too small
#endif

struct psram_buffer_s *psram_buffer;

void psram_buffer_init(struct psram_buffer_s *old_psram_buffer) {
  if (old_psram_buffer == NULL) {
	  psram_buffer = (struct psram_buffer_s *)ps_malloc(PSRAM_BUFFER_SIZE);
  }
  else {
	  psram_buffer = old_psram_buffer;
  }
  // check version
  uint8_t is_ok = 1;
  if ((psram_buffer->version & 0xff00) != (PSRAM_VERSION & 0xff00)) {
    is_ok = 0;
  } else if (psram_buffer->version > PSRAM_VERSION) {
    // not compatible
    is_ok = 0;
  }
  if (!is_ok) {
    psram_buffer->version = PSRAM_VERSION;
    psram_buffer->windex = 0;
    psram_buffer->rindex = 0;
    psram_buffer->steigung = 0;
    psram_buffer->cumulated_consumption = 0;
    psram_buffer->last_timestamp_no_consumption = 0;
    psram_buffer->last_timestamp_no_consumption_all_pointers = 0;
  }
}

#define ANG_RANGE 200
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

int8_t psram_buffer_add(uint32_t timestamp, uint16_t angle0, uint16_t angle1,
                        uint16_t angle2, uint16_t angle3) {
  if (psram_buffer->windex - NUM_ENTRIES == psram_buffer->rindex) {
    psram_buffer->rindex++;
  }
  struct entry_s *e = &ENTRY(psram_buffer->windex);
  psram_buffer->windex++;
  e->timestamp = timestamp;
  uint8_t norm_angle0 = angle0 * ANG_STEP / 9;
  uint8_t norm_angle1 = angle1 * ANG_STEP / 9;
  uint8_t norm_angle2 = angle2 * ANG_STEP / 9;
  uint8_t norm_angle3 = angle3 * ANG_STEP / 9;
  e->angle[0] = norm_angle0;
  e->angle[1] = norm_angle1;
  e->angle[2] = norm_angle2;
  e->angle[3] = norm_angle3;

  uint16_t num_data = psram_buffer->windex - psram_buffer->rindex;
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
    uint16_t j1 = psram_buffer->windex + NUM_ENTRIES - window + i;
    uint16_t j2 = psram_buffer->windex + NUM_ENTRIES - window + i + 1;
    uint8_t a1 = ENTRY(j1).angle[0];
    uint8_t a2 = ENTRY(j2).angle[0];
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
    return -2;
  }
  uint16_t angle[WINDOW_SIZE];
  for (uint16_t i = 0; i < window; i++) {
    angle[i] = 0;
  }
  {
    uint16_t j1 = psram_buffer->windex + NUM_ENTRIES - window + i_first;
    angle[i_first] = ENTRY(j1).angle[0];
    uint16_t last_angle = angle[i_first];
    for (uint16_t i = i_first + 1; i <= i_last; i++) {
      if (err[i] != 0) {
        continue;
      }
      uint16_t j = psram_buffer->windex + NUM_ENTRIES - window + i;
      uint16_t this_angle = ENTRY(j).angle[0];
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
    uint16_t j1 = psram_buffer->windex + NUM_ENTRIES - window + i_first;
    uint16_t j2 = psram_buffer->windex + NUM_ENTRIES - window + i_last;

    uint32_t a1 = angle[i_first];
    uint32_t a2 = angle[i_last];
    uint32_t t1 = ENTRY(j1).timestamp;
    uint32_t t2 = ENTRY(j2).timestamp;

    if (a2 > a1) {
      s = a2 - a1;
    }
    s *= 10000 / ANG_RANGE;  // for 0.1 m3
    s *= 3600;
    s /= (t2 - t1);
    psram_buffer->steigung = s;
  }

  if (err[window - 1] != 0) {
    return -3;
  }

  if (psram_buffer->steigung == 0) {
    psram_buffer->last_timestamp_no_consumption = timestamp;
    psram_buffer->cumulated_consumption = 0;
  } else {
    uint16_t j = psram_buffer->windex + NUM_ENTRIES - 2;
    uint32_t t = ENTRY(j).timestamp;
    psram_buffer->cumulated_consumption +=
        psram_buffer->steigung * (timestamp - t) / 3600;
  }

  for (uint16_t i = psram_buffer->rindex;
       (i & NUM_ENTRIES_MASK) != (psram_buffer->windex & NUM_ENTRIES_MASK);
       i++) {
    struct entry_s *e = &ENTRY(i);
    if ((e->angle[0] == norm_angle0) && (e->angle[1] == norm_angle1) &&
        (e->angle[2] == norm_angle2) && (e->angle[3] == norm_angle3)) {
      if (timestamp - e->timestamp > 3600) {
        psram_buffer->last_timestamp_no_consumption_all_pointers = timestamp;
      }
      if (timestamp - e->timestamp > 1200) {
        psram_buffer->cumulated_consumption = 0;
      }
      break;
    }
  }

  return 0;
}

uint16_t water_consumption() {
  // return psram_buffer->entry[(psram_buffer->windex+NUM_ENTRIES-1) &
  // NUM_ENTRIES_MASK].angle[0];
  return psram_buffer->cumulated_consumption;
}

uint8_t have_alarm() {
  if (psram_buffer->cumulated_consumption > 1300) {
    return ALARM_CUMULATED_CONSUMPTION_TOO_HIGH;
  }
  if (psram_buffer->steigung > 700) {
    return ALARM_TOO_HIGH_CONSUMPTION;
  }
  uint16_t num_data = psram_buffer->windex - psram_buffer->rindex;
  if (num_data > NUM_ENTRIES / 2) {
    // 64*10 = 640 minutes => ~10h
    uint32_t timestamp =
        ENTRY(psram_buffer->windex + NUM_ENTRIES - 1).timestamp;
    if (timestamp - psram_buffer->last_timestamp_no_consumption_all_pointers >
        18 * 3600) {
      return ALARM_LEAKAGE_FINE;
    }
    if (timestamp - psram_buffer->last_timestamp_no_consumption > 18 * 3600) {
      return ALARM_LEAKAGE;
    }
  }
  return NO_ALARM;
}
uint32_t water_steigung() { return psram_buffer->steigung; }
uint32_t cumulated_consumption() { return psram_buffer->cumulated_consumption; }
uint16_t num_entries() {
  return ((uint16_t)(psram_buffer->windex - psram_buffer->rindex));
}
struct entry_s *get_entry(uint16_t i) {
	if (i >= num_entries()) {
		return NULL;
	}
	return &ENTRY(psram_buffer->rindex + i);
}

