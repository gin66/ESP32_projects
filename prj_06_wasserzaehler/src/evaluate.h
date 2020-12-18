// esp32 has 8k of RTC RAM
//
#include <stdint.h>
#ifdef ARDUINO_ARCH_ESP32
#include <Arduino.h>
#else
#define RTC_DATA_ATTR
#endif

#define NUM_ENTRIES 256
#define NUM_ENTRIES_MASK (NUM_ENTRIES - 1)

#define RTC_VERSION 0x0101

struct entry_s {
  uint32_t timestamp;
  uint8_t angle[4];  // 0..360° encoded as 0-100
};
struct rtc_ram_buffer_s {
  uint16_t version;
  uint16_t windex;
  uint16_t rindex;
  uint32_t steigung;
  uint32_t cumulated_consumption;
  uint32_t last_timestamp_no_consumption;
  uint32_t last_timestamp_no_consumption_all_pointers;
  struct entry_s entry[NUM_ENTRIES];
};

void rtc_ram_buffer_init();
int8_t rtc_ram_buffer_add(uint32_t timestamp,
                          uint16_t angle0, uint16_t angle1, uint16_t angle2,
                          uint16_t angle3);
uint16_t water_consumption();

#define NO_ALARM 0
#define ALARM_TOO_HIGH_CONSUMPTION 1
#define ALARM_CUMULATED_CONSUMPTION_TOO_HIGH 2
#define ALARM_LEAKAGE 3
#define ALARM_LEAKAGE_FINE 4
uint8_t have_alarm();
uint32_t water_steigung();
uint32_t cumulated_consumption();
uint16_t num_entries();

RTC_DATA_ATTR extern struct rtc_ram_buffer_s rtc_buffer;
