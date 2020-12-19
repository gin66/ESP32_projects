#include "evaluate.h"

#include <stdio.h>

RTC_DATA_ATTR struct rtc_ram_buffer_s rtc_buffer;

void rtc_ram_buffer_init() {
  if (rtc_buffer.version != RTC_VERSION) {
    rtc_buffer.version = RTC_VERSION;
    rtc_buffer.windex = 0;
    rtc_buffer.rindex = 0;
    rtc_buffer.steigung = 0;
    rtc_buffer.cumulated_consumption = 0;
    rtc_buffer.last_timestamp_no_consumption = 0;
    rtc_buffer.last_timestamp_no_consumption_all_pointers = 0;
  }
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

int8_t rtc_ram_buffer_add(uint32_t timestamp, uint16_t angle0, uint16_t angle1,
                          uint16_t angle2, uint16_t angle3) {
  if (rtc_buffer.version != RTC_VERSION) {
    return -100;
  }
  if (rtc_buffer.windex - NUM_ENTRIES == rtc_buffer.rindex) {
    rtc_buffer.rindex++;
  }
  struct entry_s *e = &rtc_buffer.entry[rtc_buffer.windex++ & NUM_ENTRIES_MASK];
  e->timestamp = timestamp;
  uint8_t norm_angle0 = angle0 * ANG_STEP / 18;
  uint8_t norm_angle1 = angle1 * ANG_STEP / 18;
  uint8_t norm_angle2 = angle2 * ANG_STEP / 18;
  uint8_t norm_angle3 = angle3 * ANG_STEP / 18;
  e->angle[0] = norm_angle0;
  e->angle[1] = norm_angle1;
  e->angle[2] = norm_angle2;
  e->angle[3] = norm_angle3;

  uint16_t num_data = rtc_buffer.windex - rtc_buffer.rindex;
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
    uint16_t j1 =
        (rtc_buffer.windex + NUM_ENTRIES - window + i) & NUM_ENTRIES_MASK;
    uint16_t j2 =
        (rtc_buffer.windex + NUM_ENTRIES - window + i + 1) & NUM_ENTRIES_MASK;
    uint8_t a1 = rtc_buffer.entry[j1].angle[0];
    uint8_t a2 = rtc_buffer.entry[j2].angle[0];
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
    uint16_t j1 =
        (rtc_buffer.windex + NUM_ENTRIES - window + i_first) & NUM_ENTRIES_MASK;
    angle[i_first] = rtc_buffer.entry[j1].angle[0];
    uint16_t last_angle = angle[i_first];
    for (uint16_t i = i_first + 1; i <= i_last; i++) {
      if (err[i] != 0) {
        continue;
      }
      uint16_t j =
          (rtc_buffer.windex + NUM_ENTRIES - window + i) & NUM_ENTRIES_MASK;
      uint16_t this_angle = rtc_buffer.entry[j].angle[0];
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
        (rtc_buffer.windex + NUM_ENTRIES - window + i_first) & NUM_ENTRIES_MASK;
    uint16_t j2 =
        (rtc_buffer.windex + NUM_ENTRIES - window + i_last) & NUM_ENTRIES_MASK;

    uint32_t a1 = angle[i_first];
    uint32_t a2 = angle[i_last];
    uint32_t t1 = rtc_buffer.entry[j1].timestamp;
    uint32_t t2 = rtc_buffer.entry[j2].timestamp;

    if (a2 > a1) {
      s = a2 - a1;
    }
    s *= 10000 / ANG_RANGE;  // for 0.1 m3
    s *= 3600;
    s /= (t2 - t1);
    rtc_buffer.steigung = s;
  }

  if (err[window - 1] != 0) {
    return -3;
  }

  if (rtc_buffer.steigung == 0) {
    rtc_buffer.last_timestamp_no_consumption = timestamp;
    rtc_buffer.cumulated_consumption = 0;
  } else {
    uint16_t j = (rtc_buffer.windex + NUM_ENTRIES - 2) & NUM_ENTRIES_MASK;
    uint32_t t = rtc_buffer.entry[j].timestamp;
    rtc_buffer.cumulated_consumption +=
        rtc_buffer.steigung * (timestamp - t) / 3600;
  }

  for (uint8_t i = rtc_buffer.rindex;
       (i & NUM_ENTRIES_MASK) != (rtc_buffer.windex & NUM_ENTRIES_MASK); i++) {
    struct entry_s *e = &rtc_buffer.entry[i & NUM_ENTRIES_MASK];
    if ((e->angle[0] == norm_angle0) && (e->angle[1] == norm_angle1) &&
        (e->angle[2] == norm_angle2) && (e->angle[3] == norm_angle3)) {
      if (timestamp - e->timestamp > 3600) {
        rtc_buffer.last_timestamp_no_consumption_all_pointers = timestamp;
      }
      if (timestamp - e->timestamp > 1200) {
        rtc_buffer.cumulated_consumption = 0;
      }
      break;
    }
  }

  return 0;
}

uint16_t water_consumption() {
  // return rtc_buffer.entry[(rtc_buffer.windex+NUM_ENTRIES-1) &
  // NUM_ENTRIES_MASK].angle[0];
  return rtc_buffer.cumulated_consumption;
}

uint8_t have_alarm() {
  if (rtc_buffer.cumulated_consumption > 1300) {
    return ALARM_CUMULATED_CONSUMPTION_TOO_HIGH;
  }
  if (rtc_buffer.steigung > 700) {
    return ALARM_TOO_HIGH_CONSUMPTION;
  }
  uint16_t num_data = rtc_buffer.windex - rtc_buffer.rindex;
  if (num_data > NUM_ENTRIES / 2) {
    // 64*10 = 640 minutes => ~10h
    uint32_t timestamp =
        rtc_buffer
            .entry[(rtc_buffer.windex + NUM_ENTRIES - 1) & NUM_ENTRIES_MASK]
            .timestamp;
    if (timestamp - rtc_buffer.last_timestamp_no_consumption_all_pointers >
        18 * 3600) {
      return ALARM_LEAKAGE_FINE;
    }
    if (timestamp - rtc_buffer.last_timestamp_no_consumption > 18 * 3600) {
      return ALARM_LEAKAGE;
    }
  }
  return NO_ALARM;
}
uint32_t water_steigung() { return rtc_buffer.steigung; }
uint32_t cumulated_consumption() { return rtc_buffer.cumulated_consumption; }
uint16_t num_entries() {
  return ((uint16_t)(rtc_buffer.windex - rtc_buffer.rindex));
}
