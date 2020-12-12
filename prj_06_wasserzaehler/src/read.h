#define HEIGHT 240
#define WIDTH 320

struct read_s {
	uint8_t ok;
};

void find_pointer(const uint8_t *rgb_565, uint8_t *digitized, struct read_s *read);
