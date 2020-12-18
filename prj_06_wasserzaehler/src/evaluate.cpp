#include "evaluate.h"

#include <stdio.h>

void rtc_ram_buffer_init(struct rtc_ram_buffer_s *b) {
  b->windex = 0;
  b->rindex = 0;
  b->steigung = 0;
  b->cumulated_consumption = 0;
  b->last_timestamp_no_consumption = 0;
  b->last_timestamp_no_consumption_all_pointers = 0;
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
  uint8_t norm_angle0 = angle0 * ANG_STEP / 18;
  uint8_t norm_angle1 = angle1 * ANG_STEP / 18;
  uint8_t norm_angle2 = angle2 * ANG_STEP / 18;
  uint8_t norm_angle3 = angle3 * ANG_STEP / 18;
  e->angle[0] = norm_angle0;
  e->angle[1] = norm_angle1;
  e->angle[2] = norm_angle2;
  e->angle[3] = norm_angle3;

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

    uint32_t a1 = angle[i_first];
    uint32_t a2 = angle[i_last];
    uint32_t t1 = b->entry[j1].timestamp;
    uint32_t t2 = b->entry[j2].timestamp;

    if (a2 > a1) {
      s = a2 - a1;
    }
    s *= 10000/ANG_RANGE; // for 0.1 m3
    s *= 3600;
    s /= (t2 - t1);
    b->steigung = s;
  }

  if (err[window - 1] != 0) {
    return -1;
  }

  if (b->steigung == 0) {
	  b->last_timestamp_no_consumption = timestamp;
	  b->cumulated_consumption = 0;
  }
  else {
	  uint16_t j = (b->windex + NUM_ENTRIES - 2) & NUM_ENTRIES_MASK;
	  uint32_t t = b->entry[j].timestamp;
	  b->cumulated_consumption += b->steigung * (timestamp - t) / 3600;
  }

  for (uint8_t i = b->rindex;(i & NUM_ENTRIES_MASK) != (b->windex & NUM_ENTRIES_MASK);i++) {
	  struct entry_s *e = &b->entry[i & NUM_ENTRIES_MASK];
	  if ((e->angle[0] == norm_angle0)
	        && (e->angle[1] == norm_angle1)
	        && (e->angle[2] == norm_angle2)
	        && (e->angle[3] == norm_angle3)) {
		  if (num_data > NUM_ENTRIES/2) {
			  // 64*10 = 640 minutes => ~10h
			  if (timestamp - e->timestamp > 3600) {
				  b->last_timestamp_no_consumption_all_pointers = timestamp;
			  }
		  }
		  if (timestamp - e->timestamp > 1200) {
			  b->cumulated_consumption = 0;
		  }
		  break;
	  }
  }

  return 0;
}

uint16_t water_consumption(struct rtc_ram_buffer_s *b) {
  // return b->entry[(b->windex+NUM_ENTRIES-1) & NUM_ENTRIES_MASK].angle[0];
  return b->cumulated_consumption;
}

uint8_t have_alarm(struct rtc_ram_buffer_s *b) {
	if (b->cumulated_consumption > 1300) {
		return ALARM_CUMULATED_CONSUMPTION_TOO_HIGH;
	}
	if (b->steigung > 700) {
		return ALARM_TOO_HIGH_CONSUMPTION;
	}
	uint32_t timestamp = b->entry[(b->windex+NUM_ENTRIES-1)&NUM_ENTRIES_MASK].timestamp;
	if (timestamp - b->last_timestamp_no_consumption_all_pointers > 18*3600) {
		return ALARM_LEAKAGE_FINE;
	}
	if (timestamp - b->last_timestamp_no_consumption > 18*3600) {
		return ALARM_LEAKAGE;
	}
	return NO_ALARM;
}

