/*
 * Host burst tests for the SML reassembly (S19.04) and the loss counters
 * (S19.05). The fixture is built with libsml itself, then framed exactly the
 * way the meter sends it: 2-byte length, 8-byte escape header, body, 6 filler
 * bytes, CRC-16. The 258-byte message is split into 33 CAN frames.
 */

#include <sml/sml_crc16.h>
#include <sml/sml_file.h>
#include <sml/sml_get_list_response.h>
#include <sml/sml_list.h>
#include <sml/sml_message.h>
#include <sml/sml_number.h>
#include <sml/sml_octet_string.h>
#include <sml/sml_value.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app/sml_power.h"

static int s_failures;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
      s_failures++;                                                            \
    }                                                                          \
  } while (0)

static sml_list *make_entry(const unsigned char obis[6], int8_t scaler,
                            uint32_t raw) {
  sml_list *entry = sml_list_init();
  entry->obj_name = sml_octet_string_init((unsigned char *)obis, 6);
  entry->scaler = sml_i8_init(scaler);
  entry->value = sml_value_init();
  entry->value->type = SML_TYPE_UNSIGNED | SML_TYPE_NUMBER_32;
  entry->value->data.uint32 = sml_u32_init(raw);
  return entry;
}

/* Serialise a GET_LIST_RESPONSE carrying the three OBIS values. */
static size_t build_body(unsigned char *out, size_t cap) {
  static const unsigned char kConsumption[6] = {1, 0, 1, 8, 0, 255};
  static const unsigned char kProduction[6] = {1, 0, 2, 8, 0, 255};
  static const unsigned char kPower[6] = {1, 0, 16, 7, 0, 255};
  unsigned char server_id[6] = {1, 2, 3, 4, 5, 6};

  sml_file *file = sml_file_init();
  sml_message *message = sml_message_init();
  sml_get_list_response *response = sml_get_list_response_init();
  response->server_id = sml_octet_string_init(server_id, 6);

  sml_list *list = make_entry(kConsumption, -1, 123456);
  sml_list_add(list, make_entry(kProduction, -1, 0));
  sml_list_add(list, make_entry(kPower, -1, 2500));
  response->val_list = list;

  message->message_body =
      sml_message_body_init(SML_MESSAGE_GET_LIST_RESPONSE, response);
  sml_file_add_message(file, message);
  sml_file_write(file);

  const size_t len = file->buf->cursor;
  if (len > cap) {
    fprintf(stderr, "fixture body %zu does not fit in %zu\n", len, cap);
    s_failures++;
    sml_file_free(file);
    return 0;
  }
  memcpy(out, file->buf->buffer, len);
  sml_file_free(file);
  return len;
}

/* Frame a body into the 258-byte meter message and append the transport CRC. */
static size_t make_wire(unsigned char *wire, size_t cap,
                        const unsigned char *body, size_t body_len) {
  static const unsigned char kHeader[8] = {0x1b, 0x1b, 0x1b, 0x1b,
                                           0x01, 0x01, 0x01, 0x01};
  const size_t total = SML_POWER_BYTES; /* 258 -> 33 CAN frames */
  const size_t valid = total - 2;
  if (cap < total || body_len > valid - 16)
    return 0;

  memset(wire, 0, total);
  /* The meter's length field excludes its own two bytes (project 15:
   * expected_length + 2 == received). */
  wire[0] = (unsigned char)(valid & 0xFF);
  wire[1] = (unsigned char)((valid >> 8) & 0xFF);
  memcpy(&wire[2], kHeader, sizeof(kHeader));
  memcpy(&wire[10], body, body_len);

  const uint16_t crc = sml_crc16_calculate(&wire[2], valid - 2);
  wire[valid] = (unsigned char)(crc >> 8);
  wire[valid + 1] = (unsigned char)(crc & 0xFF);
  return total;
}

static void feed(const unsigned char *wire, size_t total) {
  for (size_t off = 0; off < total; off += 8) {
    const size_t left = total - off;
    const uint8_t len = (uint8_t)(left > 8 ? 8 : left);
    sml_power_on_frame(CAN_ID_STROMZAEHLER_INFO_BASIS | (uint8_t)(off / 8),
                       &wire[off], len);
  }
}

static void test_burst_reassembles(void) {
  sml_power_reset();

  unsigned char body[SML_POWER_BYTES - 16] = {0};
  const size_t body_len = build_body(body, sizeof(body));
  CHECK(body_len > 0);

  unsigned char wire[SML_POWER_BYTES];
  const size_t total = make_wire(wire, sizeof(wire), body, body_len);
  CHECK(total == SML_POWER_BYTES);
  CHECK((total + 7) / 8 == 33);

  feed(wire, total);

  const sml_power_reading_t *r = sml_power_reading();
  CHECK(r->messages == 1);
  CHECK(r->errors == 0);
  CHECK(r->lost_bytes == 0);
  CHECK(r->empty_messages == 0);
  CHECK(r->valid);
  CHECK(fabs(r->consumption_wh - 12345.6) < 0.01);
  CHECK(fabs(r->production_wh) < 0.01);
  CHECK(fabs(r->current_w - 250.0) < 0.01);
}

static void test_loss_counters(void) {
  unsigned char body[SML_POWER_BYTES - 16] = {0};
  const size_t body_len = build_body(body, sizeof(body));
  unsigned char wire[SML_POWER_BYTES];
  const size_t total = make_wire(wire, sizeof(wire), body, body_len);
  CHECK(total == SML_POWER_BYTES);

  /* 1. An empty frame is counted once and changes nothing else. */
  sml_power_reset();
  sml_power_on_frame(CAN_ID_STROMZAEHLER_INFO_BASIS, wire, 0);
  CHECK(sml_power_reading()->empty_messages == 1);
  CHECK(sml_power_reading()->lost_bytes == 0);
  CHECK(sml_power_reading()->errors == 0);

  /* 2. An orphan continuation is not absorbed silently. */
  sml_power_reset();
  sml_power_on_frame(CAN_ID_STROMZAEHLER_INFO_BASIS | 4, &wire[32], 8);
  CHECK(sml_power_reading()->lost_bytes == 8);
  CHECK(sml_power_reading()->messages == 0);

  /* 3. A fourth message start abandons the oldest partial slot. */
  sml_power_reset();
  for (int i = 0; i < SML_POWER_POOL + 1; i++) {
    sml_power_on_frame(CAN_ID_STROMZAEHLER_INFO_BASIS, wire, 8);
  }
  CHECK(sml_power_reading()->lost_bytes == 8);
  CHECK(sml_power_reading()->messages == 0);

  /* 4. One corrupt CRC is one error, not one per frame. */
  sml_power_reset();
  unsigned char corrupt[SML_POWER_BYTES];
  memcpy(corrupt, wire, total);
  corrupt[40] ^= 0x01;
  feed(corrupt, total);
  CHECK(sml_power_reading()->errors == 1);
  CHECK(sml_power_reading()->lost_bytes == 0);
  CHECK(sml_power_reading()->messages == 0);
}

int main(void) {
  test_burst_reassembles();
  test_loss_counters();
  if (s_failures) {
    fprintf(stderr, "%d check(s) failed\n", s_failures);
    return 1;
  }
  printf("sml_power: burst + loss counters OK\n");
  return 0;
}
