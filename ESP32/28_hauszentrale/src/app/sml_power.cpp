/**
 * @file sml_power.cpp
 * @brief OBIS matching lifted from 15_power_log_eink/src/main.cpp (S19.01).
 *
 * Deliberately side-effect free apart from the reading it publishes: counters
 * for lost/empty messages live in the caller (S19.05), which is also where the
 * reassembly happens (S19.04).
 */

#include "sml_power.h"

#include <math.h>
#include <sml/sml_crc16.h>
#include <sml/sml_file.h>
#include <stdio.h>
#include <string.h>

static sml_power_reading_t s_reading;

typedef struct {
  uint16_t base_id;
  uint16_t expected; /* message length + the two length bytes */
  uint16_t received;
  uint16_t frames;
  bool active;
  uint8_t buffer[SML_POWER_BYTES];
} sml_slot_t;

static sml_slot_t s_pool[SML_POWER_POOL];

static const uint8_t kSmlHeader[8] = {0x1b, 0x1b, 0x1b, 0x1b,
                                      0x01, 0x01, 0x01, 0x01};

void sml_power_reset(void) {
  memset(&s_reading, 0, sizeof(s_reading));
  memset(s_pool, 0, sizeof(s_pool));
}

static bool obis_matches(const sml_list* entry, const char* wanted) {
  if (!entry || !entry->obj_name) return false;
  char obis[32];
  const uint8_t* s = entry->obj_name->str;
  snprintf(obis, sizeof(obis), "%d-%d:%d.%d.%d/%d", s[0], s[1], s[2], s[3],
           s[4], s[5]);
  return strcmp(obis, wanted) == 0;
}

bool sml_power_parse(const uint8_t* body, size_t len) {
  if (!body || len == 0) {
    s_reading.parse_errors++;
    return false;
  }

  /* libsml predates const-correctness; it only reads the buffer. */
  sml_file* file = sml_file_parse((unsigned char*)body, len);
  if (!file) {
    s_reading.parse_errors++;
    return false;
  }

  bool published = false;

  for (int i = 0; i < file->messages_len; i++) {
    sml_message* message = file->messages[i];
    if (!message || !message->message_body) continue;
    if (*message->message_body->tag != SML_MESSAGE_GET_LIST_RESPONSE) continue;

    sml_get_list_response* response =
        (sml_get_list_response*)message->message_body->data;
    for (sml_list* entry = response->val_list; entry != NULL;
         entry = entry->next) {
      if (!entry->value) continue;
      const uint8_t type = entry->value->type & SML_TYPE_FIELD;
      if (type != SML_TYPE_INTEGER && type != SML_TYPE_UNSIGNED) continue;

      const int scaler = entry->scaler ? *entry->scaler : 0;
      double value = sml_value_to_double(entry->value) * pow(10, scaler);

      if (obis_matches(entry, "1-0:1.8.0/255")) {
        s_reading.consumption_wh = value;
        s_reading.has_consumption = true;
        published = true;
      } else if (obis_matches(entry, "1-0:2.8.0/255")) {
        s_reading.production_wh = value;
        s_reading.has_production = true;
        published = true;
      } else if (obis_matches(entry, "1-0:16.7.0/255")) {
        if (entry->status && ((*entry->status->data.status16 & 0x20) != 0)) {
          value = -value;
        }
        s_reading.current_w = value;
        published = true;
      } else if (obis_matches(entry, "1-0:32.7.0/255")) {
        s_reading.voltage_v[0] = value;
        s_reading.has_voltage = true;
        published = true;
      } else if (obis_matches(entry, "1-0:52.7.0/255")) {
        s_reading.voltage_v[1] = value;
        s_reading.has_voltage = true;
        published = true;
      } else if (obis_matches(entry, "1-0:72.7.0/255")) {
        s_reading.voltage_v[2] = value;
        s_reading.has_voltage = true;
        published = true;
      } else if (obis_matches(entry, "1-0:31.7.0/255")) {
        s_reading.current_a[0] = value;
        s_reading.has_current = true;
        published = true;
      } else if (obis_matches(entry, "1-0:51.7.0/255")) {
        s_reading.current_a[1] = value;
        s_reading.has_current = true;
        published = true;
      } else if (obis_matches(entry, "1-0:71.7.0/255")) {
        s_reading.current_a[2] = value;
        s_reading.has_current = true;
        published = true;
      } else if (obis_matches(entry, "1-0:81.7.1/255")) {
        s_reading.phase_deg[0] = value;
        s_reading.has_phase = true;
        published = true;
      } else if (obis_matches(entry, "1-0:81.7.2/255")) {
        s_reading.phase_deg[1] = value;
        s_reading.has_phase = true;
        published = true;
      } else if (obis_matches(entry, "1-0:81.7.3/255")) {
        s_reading.phase_deg[2] = value;
        s_reading.has_phase = true;
        published = true;
      } else if (obis_matches(entry, "1-0:14.7.0/255")) {
        s_reading.frequency_hz = value;
        s_reading.has_frequency = true;
        published = true;
      } else if (obis_matches(entry, "1-0:1.8.0/96")) {
        s_reading.history_wh[0] = value;
        s_reading.history_mask |= 0x01;
        published = true;
      } else if (obis_matches(entry, "1-0:1.8.0/97")) {
        s_reading.history_wh[1] = value;
        s_reading.history_mask |= 0x02;
        published = true;
      } else if (obis_matches(entry, "1-0:1.8.0/98")) {
        s_reading.history_wh[2] = value;
        s_reading.history_mask |= 0x04;
        published = true;
      } else if (obis_matches(entry, "1-0:1.8.0/99")) {
        s_reading.history_wh[3] = value;
        s_reading.history_mask |= 0x08;
        published = true;
      }
    }
  }

  sml_file_free(file);
  s_reading.messages++;
  if (published) s_reading.valid = true;
  return published;
}

static bool parse_slot(sml_slot_t* slot) {
  if (slot->expected <= 18 || slot->expected > SML_POWER_BYTES) {
    s_reading.errors++;
    return false;
  }

  /* Use the length field, not the bytes actually seen: an extra frame must
   * not move the CRC window. */
  const uint16_t valid = (uint16_t)(slot->expected - 2);
  if (valid <= 16 || memcmp(&slot->buffer[2], kSmlHeader, 8) != 0) {
    s_reading.errors++;
    s_reading.header_errors++;
    return false;
  }

  const uint16_t crc =
      sml_crc16_calculate(&slot->buffer[2], (size_t)(valid - 2));
  if ((uint8_t)(crc >> 8) != slot->buffer[valid] ||
      (uint8_t)(crc & 0xFF) != slot->buffer[valid + 1]) {
    s_reading.errors++;
    s_reading.crc_errors++;
    return false;
  }

  return sml_power_parse(&slot->buffer[2 + 8], (size_t)(valid - 16));
}

void sml_power_on_frame(uint32_t id, const uint8_t* data, uint8_t len) {
  if ((id & ~0x1FFu) != CAN_ID_STROMZAEHLER_INFO_BASIS) return;

  if (len == 0) {
    s_reading.empty_messages++;
    return;
  }
  if (!data || len > 8) return;

  const uint16_t base = (uint16_t)(id & ~0xFFu);
  const uint16_t off = (uint16_t)((id & 0xFFu) * 8u);

  sml_slot_t* slot = NULL;
  if (off == 0) {
    for (int i = 0; i < SML_POWER_POOL; i++) {
      if (!s_pool[i].active) {
        slot = &s_pool[i];
        break;
      }
    }
    if (!slot) {
      /* All three slots still hold partial messages. Abandon the oldest and
       * account for the bytes we drop rather than silently absorbing them. */
      slot = &s_pool[0];
      s_reading.lost_bytes += slot->received;
    }
    memset(slot, 0, sizeof(*slot));
    slot->base_id = base;
    slot->active = true;
  } else {
    for (int i = 0; i < SML_POWER_POOL; i++) {
      if (s_pool[i].active && s_pool[i].base_id == base) {
        slot = &s_pool[i];
        break;
      }
    }
    if (!slot) {
      s_reading.lost_bytes += len;
      return;
    }
  }

  if ((size_t)off + len > sizeof(slot->buffer)) {
    /* A bogus offset would write past the buffer; abandon the message. */
    slot->active = false;
    s_reading.lost_bytes += slot->received;
    return;
  }
  memcpy(&slot->buffer[off], data, len);
  slot->received = (uint16_t)(slot->received + len);
  slot->frames++;

  if (off == 0 && len >= 2) {
    const uint16_t msg_len = (uint16_t)(data[0] | ((uint16_t)data[1] << 8));
    slot->expected = (uint16_t)(msg_len + 2u);
  }

  if (slot->expected > 0 && slot->received >= slot->expected) {
    (void)parse_slot(slot);
    slot->active = false;
  }
}

const sml_power_reading_t* sml_power_reading(void) { return &s_reading; }
