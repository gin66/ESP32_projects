#define HEIGHT 296
#define WIDTH 400

struct read_s {
  uint8_t ok;
  uint8_t candidates;
  struct pointer_s {
	  uint16_t row;
	  uint16_t col;
  } pointer[10];
};

void digitize(const uint8_t *bgr_888, uint8_t *digitized, struct read_s *read);
void find_pointer(uint8_t *digitized, uint8_t *filtered, uint8_t *temp,
                  struct read_s *read);
