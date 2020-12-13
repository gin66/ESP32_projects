#define HEIGHT 296
#define WIDTH 400

#include <stdint.h>

struct pointer_s {
  int16_t row_from, row_to, row_center2;
  int16_t col_from, col_to, col_center2;
  uint16_t angle;
};
struct read_s {
  uint8_t candidates;
  uint16_t radius2;
  struct pointer_s pointer[6];
};

void digitize(const uint8_t *bgr_888, uint8_t *digitized, struct read_s *read);
void find_pointer(uint8_t *digitized, uint8_t *filtered, uint8_t *temp,
                  struct read_s *read);
