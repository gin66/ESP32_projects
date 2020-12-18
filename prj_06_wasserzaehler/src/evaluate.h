// esp32 has 8k of RTC RAM
//
#include <stdint.h>

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

void rtc_ram_buffer_init(struct rtc_ram_buffer_s *b);
int8_t rtc_ram_buffer_add(struct rtc_ram_buffer_s *b, uint32_t timestamp,
                          uint16_t angle0, uint16_t angle1, uint16_t angle2,
                          uint16_t angle3);
uint16_t water_consumption(struct rtc_ram_buffer_s *b);

#define NO_ALARM 0
#define ALARM_TOO_HIGH_CONSUMPTION 1
#define ALARM_CUMULATED_CONSUMPTION_TOO_HIGH 2
#define ALARM_LEAKAGE 3
#define ALARM_LEAKAGE_FINE 4
uint8_t have_alarm(struct rtc_ram_buffer_s *b);

