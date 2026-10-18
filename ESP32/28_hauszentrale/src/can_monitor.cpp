/**
 * @file can_monitor.cpp
 * @brief Lock-free CAN RX ring, per-ID table, decoder and SML reassembly.
 *
 * See can_monitor.h for the threading contract. The decoder is pure: all
 * counters and all SML reassembly live on the RX/consumer side.
 */

#include "can_monitor.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

constexpr uint8_t kSmlHeader[8] = {0x1b, 0x1b, 0x1b, 0x1b,
                                   0x01, 0x01, 0x01, 0x01};

/* A frame as it crosses the ring. 16 bytes including padding. */
struct Frame {
  uint32_t id;
  uint8_t len;
  uint8_t ext;
  uint8_t data[8];
};

Frame s_ring[CAN_MONITOR_RING_SLOTS];
std::atomic<uint8_t> s_head{0};
std::atomic<uint8_t> s_tail{0};
std::atomic<uint32_t> s_dropped{0};

/* Serialises the consumer's table/SML writes against the renderer's copies.
 * The ring itself needs no lock (SPSC indices only). */
portMUX_TYPE s_data_lock = portMUX_INITIALIZER_UNLOCKED;

can_id_entry_t s_table[CAN_MONITOR_MAX_IDS];
int s_count = 0;
sml_reassembly_t s_sml[CAN_MONITOR_SML_SLOTS];
can_monitor_counters_t s_counters;
bool s_task_started = false;
volatile bool s_reset_pending = false;

/* ── Ring ────────────────────────────────────────────────────────────────── */

bool ring_pop(Frame* out) {
  const uint8_t tail = s_tail.load(std::memory_order_relaxed);
  if (tail == s_head.load(std::memory_order_acquire)) return false;
  *out = s_ring[tail];
  s_tail.store((uint8_t)((tail + 1u) % CAN_MONITOR_RING_SLOTS),
               std::memory_order_release);
  return true;
}

void ring_push(const Frame& f) {
  const uint8_t head = s_head.load(std::memory_order_relaxed);
  const uint8_t next = (uint8_t)((head + 1u) % CAN_MONITOR_RING_SLOTS);
  if (next == s_tail.load(std::memory_order_acquire)) {
    s_dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  s_ring[head] = f;
  s_head.store(next, std::memory_order_release);
}

/* ── Table ───────────────────────────────────────────────────────────────── */

void table_update(const Frame& f) {
  portENTER_CRITICAL(&s_data_lock);
  can_id_entry_t* entry = nullptr;
  for (int i = 0; i < s_count; i++) {
    if (s_table[i].used && s_table[i].identifier == f.id) {
      entry = &s_table[i];
      break;
    }
  }

  if (entry == nullptr && s_count < CAN_MONITOR_MAX_IDS) {
    entry = &s_table[s_count++];
    std::memset(entry, 0, sizeof(*entry));
    entry->used = true;
    entry->identifier = f.id;
    entry->is_extended = f.ext != 0;
  }
  if (entry == nullptr) {
    portEXIT_CRITICAL(&s_data_lock);
    return; /* table full: keep decoding/counting anyway */
  }

  const unsigned long now = millis();
  if (entry->last_timestamp != 0) {
    entry->interval_ms = now - entry->last_timestamp;
    if (entry->interval_ms > 0) {
      if (entry->frame_count == 0 ||
          entry->interval_ms < entry->interval_min_ms) {
        entry->interval_min_ms = entry->interval_ms;
      }
      if (entry->frame_count == 0 ||
          entry->interval_ms > entry->interval_max_ms) {
        entry->interval_max_ms = entry->interval_ms;
      }
    }
  }
  entry->last_timestamp = now;

  entry->data_changed = (entry->data_len != f.len ||
                         std::memcmp(entry->data, f.data, f.len) != 0);

  std::memcpy(entry->last_data, entry->data, sizeof(entry->last_data));
  std::memset(entry->data, 0, sizeof(entry->data));
  std::memcpy(entry->data, f.data, f.len);
  entry->data_len = f.len;

  entry->frame_count++;
  if (entry->data_changed) entry->changed_count++;
  portEXIT_CRITICAL(&s_data_lock);
}

/* ── SML reassembly (bus order, per base id) ─────────────────────────────── */

bool sml_header_ok(const uint8_t* buf, size_t n) {
  return n >= 16 && std::memcmp(buf, kSmlHeader, sizeof(kSmlHeader)) == 0;
}

void sml_on_frame(uint32_t id, const uint8_t* data, uint8_t len) {
  const uint16_t base = (uint16_t)(id & ~0xFFu);
  const int slot = (base == CAN_ID_STROMZAEHLER_INFO_ALT) ? 1 : 0;
  sml_reassembly_t* s = &s_sml[slot];

  if (len == 0) {
    s_counters.sml_empty++;
    return;
  }

  portENTER_CRITICAL(&s_data_lock);
  const uint16_t off = (uint16_t)((id & 0xFFu) * 8u);
  if (off == 0) {
    std::memset(s, 0, sizeof(*s));
    s->base_id = base;
    s->active = true;
  }

  if ((size_t)off + len <= sizeof(s->buffer)) {
    std::memcpy(&s->buffer[off], data, len);
  }
  s->received_length = (uint16_t)(s->received_length + len);
  s->frames_received++;

  if (off == 0 && len >= 2) {
    const uint16_t msg_len = (uint16_t)(data[0] | ((uint16_t)data[1] << 8));
    s->expected_length = (uint16_t)(msg_len + 2u);
  }

  if (s->active && s->expected_length > 0 &&
      s->received_length >= s->expected_length) {
    s->complete = sml_header_ok(s->buffer, s->received_length);
    s->active = false;
    if (s->complete) {
      s_counters.sml_ok++;
    } else {
      s_counters.sml_incomplete++;
    }
  }
  portEXIT_CRITICAL(&s_data_lock);
}

/* ── Consumer ────────────────────────────────────────────────────────────── */

void on_frame(const Frame& f) {
  s_counters.rx_frames++;

  table_update(f);

  if ((f.id & ~0x1FFu) == CAN_ID_STROMZAEHLER_INFO_BASIS) {
    sml_on_frame(f.id, f.data, f.len);
    return;
  }

  can_monitor_decoded_t d;
  can_monitor_decode(f.id, f.data, f.len, &d);

  switch (d.protocol) {
    case HAUS_PROTO_HEIZUNG_ID:
      s_counters.proto_id++;
      break;
    case HAUS_PROTO_HEIZUNG_TEMP:
      s_counters.proto_temp++;
      break;
    case HAUS_PROTO_HEIZUNG_PWM:
    case HAUS_PROTO_HEIZUNG_PWM_RPT:
      s_counters.proto_pwm++;
      break;
    case HAUS_PROTO_HEIZUNG_ADC:
      s_counters.proto_adc++;
      break;
    case HAUS_PROTO_HEIZUNG_EE:
      s_counters.proto_ee++;
      if (f.len >= 8) {
        haus_can_ee_t e;
        haus_can_get_ee(f.data, &e);
        s_counters.ee_anom = (e.flags & EE_FLAG_ANOMALY) ? 1 : 0;
        s_counters.pwmfail = (e.flags & EE_FLAG_PWMFAIL) ? 1 : 0;
      }
      break;
    case HAUS_PROTO_HEIZUNG_STATS:
      s_counters.proto_stats++;
      break;
    case HAUS_PROTO_TIMESTAMP:
      s_counters.proto_ts++;
      break;
    default:
      if (d.protocol == (haus_protocol_t)0xFFFF) {
        s_counters.proto_time_bcast++;
      } else {
        s_counters.proto_unknown++;
      }
      break;
  }
}

void can_monitor_task(void*) {
  for (;;) {
    if (s_reset_pending) {
      /* Clear from the consumer side: the producer must keep sole ownership
       * of the write index. Draining first also drops any queued frames. */
      Frame discard;
      while (ring_pop(&discard)) {
      }
      portENTER_CRITICAL(&s_data_lock);
      std::memset(s_table, 0, sizeof(s_table));
      std::memset(s_sml, 0, sizeof(s_sml));
      std::memset(&s_counters, 0, sizeof(s_counters));
      s_count = 0;
      portEXIT_CRITICAL(&s_data_lock);
      s_dropped.store(0, std::memory_order_relaxed);
      s_reset_pending = false;
    }

    Frame f;
    int n = 0;
    while (n < 32 && ring_pop(&f)) {
      on_frame(f);
      n++;
    }
    s_counters.dropped = s_dropped.load(std::memory_order_relaxed);
    vTaskDelay(pdMS_TO_TICKS(n ? 1 : 10));
  }
}

}  // namespace

/* ── Public API ──────────────────────────────────────────────────────────── */

void can_monitor_start(void) {
  if (s_task_started) return;
  s_task_started = true;
  if (xTaskCreate(can_monitor_task, "canMon", 4096, nullptr, 2, nullptr) !=
      pdPASS) {
    s_task_started = false;
  }
}

void can_monitor_push(const can_monitor_frame_t* frame) {
  if (!frame) return;
  Frame f;
  f.id = frame->id;
  f.len = frame->len <= 8 ? frame->len : 8;
  f.ext = frame->ext ? 1 : 0;
  std::memset(f.data, 0, sizeof(f.data));
  std::memcpy(f.data, frame->data, f.len);
  ring_push(f);
}

void can_monitor_reset(void) { s_reset_pending = true; }

bool can_monitor_get_entry(int index, can_id_entry_t* out) {
  if (!out) return false;
  bool ok = false;
  portENTER_CRITICAL(&s_data_lock);
  if (index >= 0 && index < s_count) {
    *out = s_table[index];
    ok = true;
  }
  portEXIT_CRITICAL(&s_data_lock);
  return ok;
}

int can_monitor_count(void) { return s_count; }

bool can_monitor_get_sml(int slot, sml_reassembly_t* out) {
  if (!out || slot < 0 || slot >= CAN_MONITOR_SML_SLOTS) return false;
  portENTER_CRITICAL(&s_data_lock);
  *out = s_sml[slot];
  portEXIT_CRITICAL(&s_data_lock);
  return true;
}

const can_monitor_counters_t* can_monitor_counters(void) { return &s_counters; }

void can_monitor_decode(uint32_t id, const uint8_t* data, uint8_t len,
                        can_monitor_decoded_t* out) {
  if (!out) return;
  std::memset(out, 0, sizeof(*out));
  out->protocol = HAUS_PROTO_NONE;

  if (!data) len = 0;
  if (len > 8) len = 8;

  const uint8_t rank = haus_can_get_rank((uint16_t)(id & 0x7FFu));
  if (rank != (uint8_t)-1) {
    out->protocol = HAUS_PROTO_HEIZUNG_ID;
    std::snprintf(out->label, sizeof(out->label), "ID 0x180+%u", rank);
    if (len >= 8) {
      haus_can_identity_t ident;
      haus_can_get_identity(data, &ident);
      std::snprintf(out->decoded, sizeof(out->decoded),
                    "%02X:%02X:%02X:%02X:%02X:%02X", ident.rom[1], ident.rom[2],
                    ident.rom[3], ident.rom[4], ident.rom[5], ident.rom[6]);
    }
    return;
  }

  const haus_protocol_t proto = (haus_protocol_t)(id & 0x7FFu);

  if ((id & ~0x1FFu) == CAN_ID_STROMZAEHLER_INFO_BASIS) {
    out->protocol = HAUS_PROTO_SML;
    std::snprintf(out->label, sizeof(out->label), "SML");
    if (len == 0) {
      std::snprintf(out->decoded, sizeof(out->decoded), "ALIVE");
      return;
    }
    const uint16_t base = (uint16_t)(id & ~0xFFu);
    const int slot = (base == CAN_ID_STROMZAEHLER_INFO_ALT) ? 1 : 0;
    sml_reassembly_t snap;
    can_monitor_get_sml(slot, &snap);
    if (snap.active && snap.expected_length > 0) {
      std::snprintf(out->decoded, sizeof(out->decoded), "SML %u/%u B (%u fr)",
                    snap.received_length, snap.expected_length,
                    snap.frames_received);
      return;
    }
    if (snap.complete) {
      std::snprintf(out->decoded, sizeof(out->decoded), "SML %u B OK",
                    snap.expected_length);
      return;
    }
    std::snprintf(out->decoded, sizeof(out->decoded), "SML %u B", len);
    return;
  }

  switch (proto) {
    case HAUS_PROTO_HEIZUNG_TEMP: {
      std::snprintf(out->label, sizeof(out->label), "TEMP");
      if (len >= 8) {
        haus_can_temperature_t t;
        haus_can_get_temperature(data, &t);
        char buf[64];
        buf[0] = '\0';
        for (int i = 0; i < HAUS_CAN_MAX_SENSORS; i++) {
          const int16_t v = t.temperature[i];
          if (v == HAUS_CAN_TEMP_EMPTY) {
            std::strncat(buf, "-- ", sizeof(buf) - std::strlen(buf) - 1);
          } else {
            char tmp[16];
            std::snprintf(tmp, sizeof(tmp), "%.2f ", v / 100.0);
            std::strncat(buf, tmp, sizeof(buf) - std::strlen(buf) - 1);
          }
        }
        std::snprintf(out->decoded, sizeof(out->decoded), "%s", buf);
      }
      return;
    }

    case HAUS_PROTO_HEIZUNG_PWM_RPT:
    case HAUS_PROTO_HEIZUNG_PWM: {
      std::snprintf(out->label, sizeof(out->label), "PWM");
      if (len >= 5) {
        haus_can_pwm_t p;
        haus_can_get_pwm(data, &p);
        const char* src;
        switch (haus_can_pwm_source(p.flags)) {
          case PWM_SRC_CAN:
            src = "CAN";
            break;
          case PWM_SRC_NRF:
            src = "NRF";
            break;
          case PWM_SRC_FAULT:
            src = "FAULT";
            break;
          default:
            src = "EEPROM";
            break;
        }
        std::snprintf(out->decoded, sizeof(out->decoded),
                      "duty=%d/1000 f=%dHz %s src=%s", p.duty_permille,
                      p.frequency_hz,
                      (p.flags & PWM_FLAG_ACTIVE) ? "ACTIVE" : "OFF", src);
      }
      return;
    }

    case HAUS_PROTO_HEIZUNG_ADC: {
      std::snprintf(out->label, sizeof(out->label), "ADC");
      if (len >= 7) {
        haus_can_adc_t a;
        haus_can_get_adc(data, &a);
        std::snprintf(out->decoded, sizeof(out->decoded),
                      "max=%u min=%u pp=%u flags=0x%02X", a.max_count,
                      a.min_count, a.pp_count, a.flags);
      }
      return;
    }

    case HAUS_PROTO_HEIZUNG_EE: {
      std::snprintf(out->label, sizeof(out->label), "EEPROM");
      if (len >= 8) {
        haus_can_ee_t e;
        haus_can_get_ee(data, &e);
        const char* src;
        switch (e.flags & EE_SRC_MASK) {
          case EE_SRC_VIRGIN << EE_SRC_SHIFT:
            src = "virgin";
            break;
          case EE_SRC_MARKER << EE_SRC_SHIFT:
            src = "marker";
            break;
          case EE_SRC_WINDOW << EE_SRC_SHIFT:
            src = "window";
            break;
          case EE_SRC_ANOMALY << EE_SRC_SHIFT:
            src = "ANOMALY";
            break;
          case EE_SRC_INVALID << EE_SRC_SHIFT:
            src = "bad-newest";
            break;
          default:
            src = "?";
            break;
        }
        std::snprintf(out->decoded, sizeof(out->decoded),
                      "rest=%u rec=%u arc=%u..%u w=%u %s%s%s", e.restored_duty,
                      e.records, e.oldest, e.newest, e.writes, src,
                      (e.oldest > e.newest) ? " wrap" : "",
                      (e.flags & EE_FLAG_PWMFAIL) ? " PWMFAIL" : "");
      }
      return;
    }

    case HAUS_PROTO_HEIZUNG_STATS: {
      std::snprintf(out->label, sizeof(out->label), "STATS");
      if (len >= 8) {
        haus_can_stats_t s;
        haus_can_get_stats(data, &s);
        std::snprintf(out->decoded, sizeof(out->decoded),
                      "sent=%u recv=%u ref=%u eflg=0x%02X canstat=0x%02X",
                      s.sent, s.received, s.refused, s.eflg, s.canstat);
      }
      return;
    }

    case HAUS_PROTO_TIMESTAMP: {
      std::snprintf(out->label, sizeof(out->label), "TS");
      if (len >= 4) {
        haus_can_timestamp_t ts;
        haus_can_get_timestamp(data, &ts);
        std::snprintf(out->decoded, sizeof(out->decoded),
                      "%02d:%02d:%02d (day %d)", ts.hours, ts.minutes,
                      ts.seconds, ts.weekday);
      }
      return;
    }

    default:
      break;
  }

  /* Non-haus time broadcast: id is the node id, data[0] is the type. */
  if (len >= 8 && data[0] == 0x01) {
    out->protocol = (haus_protocol_t)0xFFFF;
    std::snprintf(out->label, sizeof(out->label), "TIME-BCAST");
    std::snprintf(out->decoded, sizeof(out->decoded), "%02d:%02d:%02d (day %d)",
                  data[3], data[2], data[1], data[4]);
    return;
  }

  std::snprintf(out->label, sizeof(out->label), "RAW");
  size_t off = 0;
  for (uint8_t i = 0; i < len && off + 4 <= sizeof(out->decoded); i++) {
    off += std::snprintf(out->decoded + off, sizeof(out->decoded) - off,
                         "%02X ", data[i]);
  }
}
