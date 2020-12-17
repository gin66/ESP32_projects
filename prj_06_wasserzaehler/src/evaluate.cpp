
#include "evaluate.h"

void rtc_ram_buffer_init(struct rtc_ram_buffer_s *b) {
	b->windex = 0;
	b->rindex = 0;
}

void rtc_ram_buffer_add(struct rtc_ram_buffer_s *b,
		uint32_t timestamp,
		uint16_t angle0,
		uint16_t angle1,
		uint16_t angle2,
		uint16_t angle3) {
	b->windex++;
	if ((b->windex & NUM_ENTRIES_MASK) == (b->rindex & NUM_ENTRIES_MASK)) {
		b->rindex++;
	}
	struct entry_s *e = &b->entry[b->windex];
	e->timestamp = timestamp;
	e->angle[0] = angle0 >> 1;
	e->angle[1] = angle1 >> 1;
	e->angle[2] = angle2 >> 1;
	e->angle[3] = angle3 >> 1;
}

