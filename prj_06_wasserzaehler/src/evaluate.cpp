#include <stdio.h>

#include "evaluate.h"

void rtc_ram_buffer_init(struct rtc_ram_buffer_s *b) {
	b->windex = 0;
	b->rindex = 0;
}

#define ANG_STEP 5

int8_t rtc_ram_buffer_add(struct rtc_ram_buffer_s *b,
		uint32_t timestamp,
		uint16_t angle0,
		uint16_t angle1,
		uint16_t angle2,
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
	if (num_data < 2) {
		return -1;
	}

	uint16_t window = num_data;
	if (window > 10) {
		window = 10;
	}
	uint16_t a0_sum = 0;
	for (uint16_t i = 0; i < window;i++) {
		uint16_t j = (b->windex - 1 + NUM_ENTRIES - i) & NUM_ENTRIES_MASK;
		a0_sum += b->entry[j].angle[0];
	}
	a0_sum /= window;

	uint16_t da0 = 0;
	if (a0_sum > e->angle[0]) {
		da0 = a0_sum - e->angle[0];
	}
	if (a0_sum < e->angle[0]) {
		da0 = e->angle[0] - a0_sum;
	}

	if ((da0 > 2*ANG_STEP) && (da0 < 100-2*ANG_STEP)){
		return -1;
	}

	return 0;
}

uint16_t water_consumption(struct rtc_ram_buffer_s *b) {
	return b->entry[(b->windex+NUM_ENTRIES-1) & NUM_ENTRIES_MASK].angle[0];
}

