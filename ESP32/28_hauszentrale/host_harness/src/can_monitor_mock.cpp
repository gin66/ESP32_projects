/**
 * @file can_monitor_mock.cpp
 * @brief Host-side stand-in for the 28_hauszentrale CAN scope.
 *
 * The productive UI (src/ui/haus_ui.c) is shared with the firmware, so the
 * harness must provide the same data API the device implements in
 * src/can_monitor.cpp. This is a fixed synthetic bus, not a simulator: it
 * exists so the 13-row layout, the fault banner and the detail overlay render
 * in the PNGs.
 */

#include <lvgl.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "can_monitor.h"

namespace {

can_id_entry_t s_entries[16];
int s_count = 0;
sml_reassembly_t s_sml[CAN_MONITOR_SML_SLOTS];
can_monitor_adc_wave_t s_wave;
can_monitor_adc_span_t s_span;
can_monitor_counters_t s_counters;
bool s_ready = false;

unsigned long now_ms() { return (unsigned long)lv_tick_get(); }

can_id_entry_t* add(uint32_t id, bool ext, uint8_t len, const uint8_t* data) {
  can_id_entry_t* e = &s_entries[s_count++];
  memset(e, 0, sizeof(*e));
  e->used = true;
  e->identifier = id;
  e->is_extended = ext;
  e->data_len = len;
  if (data && len) memcpy(e->data, data, len);
  e->last_timestamp = now_ms();
  e->interval_ms = 500;
  e->interval_min_ms = 500;
  e->interval_max_ms = 502;
  e->frame_count = 142;
  e->changed_count = 89;
  return e;
}

void ensure(void) {
  if (s_ready) return;
  s_ready = true;

  uint8_t d[8];

  haus_can_identity_t ident;
  memset(&ident, 0, sizeof(ident));
  ident.rom[1] = 0x01;
  ident.rom[2] = 0x23;
  ident.rom[3] = 0x45;
  ident.rom[4] = 0x67;
  ident.rom[5] = 0x89;
  ident.rom[6] = 0xAB;
  haus_can_set_identity(d, &ident);
  add(0x180, false, 8, d);

  haus_can_temperature_t temps;
  temps.temperature[0] = 2150;
  temps.temperature[1] = 2230;
  temps.temperature[2] = 1975;
  temps.temperature[3] = HAUS_CAN_TEMP_EMPTY;
  haus_can_set_temperature(d, &temps);
  add(0x200, false, 8, d);

  haus_can_pwm_t pwm;
  memset(&pwm, 0, sizeof(pwm));
  pwm.duty_permille = 750;
  pwm.frequency_hz = 5000;
  pwm.flags = PWM_FLAG_ACTIVE | PWM_SRC_CAN;
  haus_can_set_pwm(d, &pwm);
  add(0x280, false, sizeof(haus_can_pwm_t), d);

  haus_can_adc_t adc;
  memset(&adc, 0, sizeof(adc));
  adc.max_count = 812;
  adc.min_count = 44;
  adc.pp_count = 768;
  haus_can_set_adc(d, &adc);
  add(0x2C0, false, sizeof(haus_can_adc_t), d);

  /* One 20 ms waveform, split into the five CAN parts. */
  uint16_t bins[CAN_MONITOR_WAVE_SLOTS];
  memset(&s_wave, 0, sizeof(s_wave));
  s_wave.valid = true;
  s_wave.generation = 1;
  s_wave.seen_ms = now_ms();
  s_span.valid = true;
  s_span.seen_ms = s_wave.seen_ms;
  s_span.pp_count = 768;
  s_span.max_count = 812;
  s_span.min_count = 44;
  for (int i = 0; i < CAN_MONITOR_WAVE_SLOTS; i++) {
    const double s =
        std::sin(2.0 * 3.14159265358979 * (double)i / (double)CAN_MONITOR_WAVE_SLOTS);
    int counts = 428 + (int)(380.0 * s);
    if (counts < 0) counts = 0;
    if (counts > 1023) counts = 1023;
    bins[i] = (uint16_t)(counts * 64);
    s_wave.slot[i] = bins[i];
  }
  for (int p = 0; p < CAN_ID_ADC_WAVE_COUNT; p++) {
    haus_can_adc_wave_t w;
    memcpy(w.bin, &bins[p * 4], sizeof(w.bin));
    haus_can_set_adc_wave(d, &w);
    add((uint32_t)CAN_ID_ADC_WAVE0 + (uint32_t)p, false, 8, d);
  }

  haus_can_stats_t stats;
  memset(&stats, 0, sizeof(stats));
  stats.sent = 1200;
  stats.received = 1180;
  stats.refused = 2;
  haus_can_set_stats(d, &stats);
  add(0x300, false, 8, d);

  haus_can_timestamp_t ts = {18, 32, 14, 2};
  haus_can_set_timestamp(d, &ts);
  add(0x555, false, 8, d);

  memset(d, 0, sizeof(d));
  d[0] = (uint8_t)(258 & 0xFF);
  d[1] = (uint8_t)(258 >> 8);
  can_id_entry_t* e = add(0x600, false, 8, d);
  e->frame_count = 33;
  memset(&s_sml[0], 0, sizeof(s_sml[0]));
  s_sml[0].base_id = 0x600;
  s_sml[0].expected_length = 258;
  s_sml[0].received_length = 258;
  s_sml[0].frames_received = 33;
  s_sml[0].complete = true;

  memset(d, 0, sizeof(d));
  d[0] = (uint8_t)(120 & 0xFF);
  d[1] = 0;
  add(0x700, false, 8, d);
  memset(&s_sml[1], 0, sizeof(s_sml[1]));
  s_sml[1].base_id = 0x700;
  s_sml[1].expected_length = 120;
  s_sml[1].received_length = 64;
  s_sml[1].frames_received = 8;
  s_sml[1].active = true;

  const uint8_t raw[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  add(0x123, false, 8, raw);

  s_counters.rx_frames = 142;
  s_counters.dropped = 0;
  s_counters.sml_ok = 7;
  s_counters.sml_incomplete = 1;
  s_counters.proto_ts = 42;
  s_counters.proto_temp = 8;
  s_counters.proto_pwm = 4;
  s_counters.proto_wave = CAN_ID_ADC_WAVE_COUNT;
}

}  // namespace

extern "C" {

void can_monitor_start(void) {}
void can_monitor_push(const can_monitor_frame_t*) {}
void can_monitor_reset(void) {}

bool can_monitor_get_entry(int index, can_id_entry_t* out) {
  ensure();
  if (!out || index < 0 || index >= s_count) return false;
  *out = s_entries[index];
  /* Keep the age column plausible: the synthetic bus keeps receiving. */
  const unsigned long now = now_ms();
  out->last_timestamp = now - (unsigned long)((index * 137u) % 3000u);
  return true;
}

int can_monitor_count(void) {
  ensure();
  return s_count;
}

bool can_monitor_get_sml(int slot, sml_reassembly_t* out) {
  ensure();
  if (!out || slot < 0 || slot >= CAN_MONITOR_SML_SLOTS) return false;
  *out = s_sml[slot];
  return true;
}

bool can_monitor_get_adc_wave(can_monitor_adc_wave_t* out) {
  ensure();
  if (!out) return false;
  *out = s_wave;
  return out->valid;
}

bool can_monitor_get_adc_span(can_monitor_adc_span_t* out) {
  ensure();
  if (!out) return false;
  *out = s_span;
  out->seen_ms = now_ms();
  return out->valid;
}

const can_monitor_counters_t* can_monitor_counters(void) {
  ensure();
  return &s_counters;
}

void can_monitor_decode(uint32_t id, const uint8_t* data, uint8_t len,
                        can_monitor_decoded_t* out) {
  if (!out) return;
  memset(out, 0, sizeof(*out));
  out->protocol = HAUS_PROTO_NONE;
  if (!data) len = 0;
  if (len > 8) len = 8;

  const uint16_t base = (uint16_t)(id & 0x7FFu);
  switch (base) {
    case 0x180:
    case 0x181:
    case 0x182:
    case 0x183:
      out->protocol = HAUS_PROTO_HEIZUNG_ID;
      snprintf(out->label, sizeof(out->label), "ID 0x180+%u", base - 0x180);
      snprintf(out->decoded, sizeof(out->decoded), "01:23:45:67:89:AB");
      return;
    case 0x200:
      out->protocol = HAUS_PROTO_HEIZUNG_TEMP;
      snprintf(out->label, sizeof(out->label), "TEMP");
      snprintf(out->decoded, sizeof(out->decoded), "21.50 22.30 19.75 -- ");
      return;
    case 0x280:
      out->protocol = HAUS_PROTO_HEIZUNG_PWM_RPT;
      snprintf(out->label, sizeof(out->label), "PWM");
      snprintf(out->decoded, sizeof(out->decoded),
               "duty=750/1000 f=5000Hz ACTIVE src=CAN");
      return;
    case 0x2C0:
      out->protocol = HAUS_PROTO_HEIZUNG_ADC;
      snprintf(out->label, sizeof(out->label), "ADC");
      snprintf(out->decoded, sizeof(out->decoded),
               "max=812 min=44 pp=768 flags=0x00");
      return;
    case 0x2C1:
    case 0x2C2:
    case 0x2C3:
    case 0x2C4:
    case 0x2C5: {
      out->protocol = (haus_protocol_t)base;
      snprintf(out->label, sizeof(out->label), "WAVE%u",
               (unsigned)(base - CAN_ID_ADC_WAVE0));
      haus_can_adc_wave_t w;
      haus_can_get_adc_wave(data, &w);
      snprintf(out->decoded, sizeof(out->decoded), "%u %u %u %u",
               (unsigned)((w.bin[0] + 32u) >> 6),
               (unsigned)((w.bin[1] + 32u) >> 6),
               (unsigned)((w.bin[2] + 32u) >> 6),
               (unsigned)((w.bin[3] + 32u) >> 6));
      return;
    }
    case 0x300:
      out->protocol = HAUS_PROTO_HEIZUNG_STATS;
      snprintf(out->label, sizeof(out->label), "STATS");
      snprintf(out->decoded, sizeof(out->decoded),
               "sent=1200 recv=1180 ref=2 eflg=0x00 canstat=0x00");
      return;
    case 0x555:
      out->protocol = HAUS_PROTO_TIMESTAMP;
      snprintf(out->label, sizeof(out->label), "TS");
      snprintf(out->decoded, sizeof(out->decoded), "14:32:18 (day 2)");
      return;
    case 0x600:
    case 0x700:
      out->protocol = HAUS_PROTO_SML;
      snprintf(out->label, sizeof(out->label), "SML");
      if (base == 0x600) {
        snprintf(out->decoded, sizeof(out->decoded), "SML 258 B OK");
      } else {
        snprintf(out->decoded, sizeof(out->decoded), "SML 64/120 B (8 fr)");
      }
      return;
    default:
      break;
  }

  if (len >= 8 && data[0] == 0x01) {
    out->protocol = (haus_protocol_t)0xFFFF;
    snprintf(out->label, sizeof(out->label), "TIME-BCAST");
    snprintf(out->decoded, sizeof(out->decoded), "14:32:18 (day 2)");
    return;
  }

  snprintf(out->label, sizeof(out->label), "RAW");
  size_t off = 0;
  for (uint8_t i = 0; i < len && off + 4 <= sizeof(out->decoded); i++) {
    off += snprintf(out->decoded + off, sizeof(out->decoded) - off, "%02X ",
                    data[i]);
  }
}

}  // extern "C"
