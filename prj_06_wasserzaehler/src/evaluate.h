// esp32 has 8k of RTC RAM
//
#include <stdint.h>

#define NUM_ENTRIES 32768
#define NUM_ENTRIES_MASK (NUM_ENTRIES - 1)

#define PSRAM_VERSION 0x0301

// Use psram as deepsleep and CPU reset persistent storage.
// Draw back is:
//		Every 8th u32 is overwritten by psram_test...

struct entry_s {
  uint32_t timestamp;
  uint8_t angle[4];  // 0..360° encoded as 0-200
  uint32_t pad1;
  uint32_t pad2;
  uint32_t pad3;
  uint32_t pad4;
  uint32_t pad5;
  uint32_t overwritten32;
};
struct psram_buffer_s {
  // first 8 u32:
  uint16_t version;
  uint16_t windex;
  uint16_t rindex;
  uint16_t pad_16;
  uint32_t steigung;
  uint32_t cumulated_consumption;
  uint32_t last_timestamp_no_consumption;
  uint32_t last_timestamp_no_consumption_all_pointers;
  uint32_t pad_32_0;
  uint32_t overwritten32_1;
  uint32_t pad_32_1;
  uint32_t pad_32_2;
  uint32_t pad_32_3;
  uint32_t pad_32_4;
  uint32_t pad_32_5;
  uint32_t pad_32_6;
  uint32_t pad_32_7;
  uint32_t overwritten32_2;

#define ENTRY(i) psram_buffer->entry_111X[(i)&NUM_ENTRIES_MASK]
  struct entry_s entry_111X[NUM_ENTRIES];
};

// must be called immediately after psramFound()
void psram_buffer_init();
int8_t psram_buffer_add(uint32_t timestamp, uint16_t angle0, uint16_t angle1,
                        uint16_t angle2, uint16_t angle3);
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

#define PSRAM_BUFFER_SIZE (NUM_ENTRIES * 32 + 2 * 32)
extern struct psram_buffer_s *psram_buffer;
