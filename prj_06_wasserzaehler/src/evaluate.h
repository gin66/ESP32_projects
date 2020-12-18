// esp32 has 8k of RTC RAM
//
#include <stdint.h>

#define NUM_ENTRIES 512
#define NUM_ENTRIES_MASK (NUM_ENTRIES - 1)

struct entry_s {
  uint32_t timestamp;
  uint8_t angle[4];  // 0..360° encoded as 0-100
};
struct rtc_ram_buffer_s {
  uint16_t windex;
  uint16_t rindex;
  uint32_t steigung;
  struct entry_s entry[NUM_ENTRIES];
};

void rtc_ram_buffer_init(struct rtc_ram_buffer_s *b);
int8_t rtc_ram_buffer_add(struct rtc_ram_buffer_s *b, uint32_t timestamp,
                          uint16_t angle0, uint16_t angle1, uint16_t angle2,
                          uint16_t angle3);
uint16_t water_consumption(struct rtc_ram_buffer_s *b);
