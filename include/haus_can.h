/**
 * @file haus_can.h
 * @brief Unified CAN bus identifiers, message structures, getter and setter
 *        functions for all house-automation projects (AVR and ESP32).
 *
 * Every project that touches the 125 kbps CAN bus must include this header
 * instead of defining its own IDs.  This guarantees that all nodes speak the
 * same language and that the CYD monitor can decode every frame.
 *
 * ─── Addressing scheme ───────────────────────────────────────────────────────
 *
 * All identifiers are standard (11-bit) CAN IDs, allocated in blocks:
 *
 *   0x001–0x17F  Reserved (future)
 *   0x180      Sensor identity (rank 0)
 *   0x181      Sensor identity (rank 1)
 *   0x182      Sensor identity (rank 2)
 *   0x183      Sensor identity (rank 3)
 *   0x184–0x1FF  Reserved (future)
 *   0x200      Temperature data (4 × int16_t c°)
 *   0x201–0x23F  Reserved (future)
 *   0x240      PWM command (set)
 *   0x241–0x27F  Reserved (future)
 *   0x280      PWM report (state)
 *   0x281–0x2BF  Reserved (future)
 *   0x2C0      ADC report
 *   0x2C1–0x2FF  Reserved (future)
 *   0x300      CAN bus health report
 *   0x301–0x554  Reserved (future)
 *   0x555      Timestamp broadcast                       [ESP32/15_power_log,
 *                                                  ESP32/16_main_control,
 *                                                  ESP32/26_CYD_CAN_monitor]
 *   0x556–0x5FF  Reserved (future)
 *   0x600      SML smart-meter data (base)               [AVR/07_stromzaehler,
 *                                                  ESP32/15_power_log,
 *                                                  ESP32/16_main_control]
 *   0x601–0x6FF  Reserved (future)
 *   0x700      SML smart-meter data (alternate)          [AVR/07_stromzaehler]
 *   0x701–0x7FF  Reserved (future)
 *   0x800+     Extended IDs (future)
 *
 * ─── Usage ───────────────────────────────────────────────────────────────────
 *
 *   Source of truth: <repo>/include/haus_can.h
 *
 *   ESP32 projects: symlinked into <project>/include/ by `make links`
 *   AVR projects:   symlinked into AVR/include/, reachable via -I../include
 *
 * Both paths are single-hop symlinks to the single source of truth.
 */

#ifndef HAUS_CAN_H
#define HAUS_CAN_H

#include <stdint.h>
#include <string.h>

/* Number of temperature sensors carried in a 0x200 frame (8 bytes / int16_t).
 * Defined here because haus_can_temperature_t depends on it. */
#define HAUS_CAN_MAX_SENSORS 4

/* ═══════════════════════════════════════════════════════════════════════════
 *  0x555  —  System-wide timestamp broadcast
 *  Sent by every ESP32 that has CAN.  One frame per second.
 *
 *  Payload (8 bytes):
 *    [0] tm_sec    (0–59)
 *    [1] tm_min    (0–59)
 *    [2] tm_hour   (0–23)
 *    [3] tm_wday   (0=Sunday … 6=Saturday)
 *    [4–7] reserved (zero)
 * ═══════════════════════════════════════════════════════════════════════════
 */
#define CAN_ID_TIMESTAMP 0x555

typedef struct __attribute__((packed)) {
  uint8_t seconds;
  uint8_t minutes;
  uint8_t hours;
  uint8_t weekday; /* 0 = Sunday */
} haus_can_timestamp_t;

static inline void haus_can_set_timestamp(uint8_t* buf,
                                          const haus_can_timestamp_t* ts) {
  buf[0] = ts->seconds;
  buf[1] = ts->minutes;
  buf[2] = ts->hours;
  buf[3] = ts->weekday;
  buf[4] = buf[5] = buf[6] = buf[7] = 0;
}

static inline void haus_can_get_timestamp(const uint8_t* buf,
                                          haus_can_timestamp_t* ts) {
  ts->seconds = buf[0];
  ts->minutes = buf[1];
  ts->hours = buf[2];
  ts->weekday = buf[3];
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  0x600 / 0x700  —  SML smart-meter (stromzaehler)
 *  The base_id toggles between 0x600 and 0x700 for alternating messages.
 *  An empty frame (DLC 0) means "alive".
 *
 *  Data frames: can_id = base_id + offset (offset = 0, 1, 2, …)
 *  First frame:  [length LSB][length MSB][data bytes…]
 *  Subsequent:   [data bytes…]  (up to 8 bytes per frame)
 * ═══════════════════════════════════════════════════════════════════════════
 */
#define CAN_ID_STROMZAEHLER_INFO_BASIS 0x600
#define CAN_ID_STROMZAEHLER_INFO_ALT 0x700

typedef struct __attribute__((packed)) {
  uint16_t message_length; /* total SML message length, little-endian */
  uint8_t data[256];       /* payload, max 256 bytes spread over frames */
} haus_can_sml_message_t;

typedef struct __attribute__((packed)) {
  uint8_t data[8];        /* raw CAN payload (6–8 bytes) */
  uint8_t len;            /* actual data bytes in this frame (1–8) */
  uint8_t is_first_frame; /* 1 if this frame carries the length header */
} haus_can_sml_frame_t;

static inline void haus_can_set_sml_frame(uint8_t* buf,
                                          const haus_can_sml_frame_t* frame) {
  memcpy(buf, frame->data, frame->len);
}

static inline void haus_can_get_sml_frame(const uint8_t* buf,
                                          haus_can_sml_frame_t* frame) {
  memcpy(frame->data, buf, 8);
  frame->len = 8;
}

static inline uint16_t haus_can_sml_get_message_length(
    const uint8_t* first_frame) {
  /* First frame of a multi-frame SML message carries length in bytes 0–1 */
  return (uint16_t)first_frame[0] | ((uint16_t)first_frame[1] << 8);
}

static inline uint16_t haus_can_sml_get_frame_offset(const uint8_t* first_frame,
                                                     const uint8_t* frame) {
  /* Offset of `frame` within the SML message.  Callers pass the first frame
   * of the same message as the origin. */
  if (first_frame == NULL || frame < first_frame) return 0;
  return (uint16_t)(frame - first_frame);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Heizung (temperature) — AVR/01_heizung_temp
 *
 *  Each sensor gets a unique rank (0 … HAUS_CAN_MAX_SENSORS-1, sorted by ROM).
 *  The rank is mapped onto CAN IDs so a receiver knows which sensor a
 *  frame belongs to without any handshake.
 * ═══════════════════════════════════════════════════════════════════════════
 */

/* 0x180 + rank  —  Sensor identity
 * DLC 8.  Sent cyclically (~1/min + burst at boot).
 *
 *  Payload (8 bytes):
 *    [0–7] DS18B20 64-bit ROM address (family 0x28 + 6-byte serial)
 * ═══════════════════════════════════════════════════════════════════════════
 */
#define CAN_ID_ID_BASE 0x180 /* + rank, 0-based */

typedef struct __attribute__((packed)) {
  uint8_t rom[8]; /* DS18B20 ROM: family(0x28) + 6-byte serial */
} haus_can_identity_t;

static inline void haus_can_set_identity(uint8_t* buf,
                                         const haus_can_identity_t* id) {
  memcpy(buf, id->rom, 8);
}

static inline void haus_can_get_identity(const uint8_t* buf,
                                         haus_can_identity_t* id) {
  memcpy(id->rom, buf, 8);
}

/* 0x200  —  Temperature data
 * DLC 8.  Sent ~1 Hz.  Empty slots carry INT16_MIN (-32768).
 *
 *  Payload (8 bytes = 4 × int16_t):
 *    [0..1] sensor 0 temperature in c° (°C × 100)
 *    [2..3] sensor 1 temperature in c°
 *    [4..5] sensor 2 temperature in c°
 *    [6..7] sensor 3 temperature in c°
 *    Empty sensor slot: INT16_MIN (-32768)
 * ═══════════════════════════════════════════════════════════════════════════
 */
#define CAN_ID_DATA_BASE 0x200

typedef struct __attribute__((packed)) {
  int16_t temperature[HAUS_CAN_MAX_SENSORS]; /* c° = °C × 100 */
} haus_can_temperature_t;

#define HAUS_CAN_TEMP_EMPTY INT16_MIN /* no sensor in slot */

static inline void haus_can_set_temperature(uint8_t* buf,
                                            const haus_can_temperature_t* t) {
  for (int i = 0; i < HAUS_CAN_MAX_SENSORS; i++) {
    buf[i * 2] = (uint8_t)(t->temperature[i] & 0xFF);
    buf[i * 2 + 1] = (uint8_t)(t->temperature[i] >> 8);
  }
}

static inline void haus_can_get_temperature(const uint8_t* buf,
                                            haus_can_temperature_t* t) {
  for (int i = 0; i < HAUS_CAN_MAX_SENSORS; i++) {
    t->temperature[i] =
        (int16_t)((uint16_t)buf[i * 2] | ((uint16_t)buf[i * 2 + 1] << 8));
  }
}

/* 0x240  —  PWM duty command (sent TO the heating controller)
 * 0x280  —  PWM output state (reported BY the heating controller)
 *  Both use the same 8-byte layout — bidirectional.
 *
 *  Payload (8 bytes):
 *    [0..1] duty in permille (uint16_t LE, 0–1000)
 *    [2..3] frequency in Hz
 *    [4]    flags (PWM_FLAG_ACTIVE = 0x01 while modulating)
 *    [5..7] reserved (zero)
 * ═══════════════════════════════════════════════════════════════════════════
 */
#define CAN_ID_PWM_SET 0x240
#define CAN_ID_PWM_REPORT 0x280

#define PWM_FLAG_ACTIVE 0x01 /* 0 < duty < 1000 permille */

/* ─── Where the duty in force came from (flags bits 1..2) ─────────────────────
 *
 * The duty is durable state, not a lease, so it is never aged out: whatever the
 * controller last decided stands until something changes it. That makes "who
 * last said so" the only thing worth knowing beyond the value itself, because it
 * answers the question a receiver actually has after a restart — was this
 * commanded by something alive, or is it simply what was persisted?
 *
 * Without this, a node that came up on its EEPROM restore and has heard nothing
 * since is indistinguishable from one that is actively being commanded, and a
 * frozen house looks exactly like a quiet one. */
#define PWM_SRC_EEPROM 0x00 /* restored from the log; no live command yet  */
#define PWM_SRC_CAN    0x02 /* last valid command was CAN 0x240            */
#define PWM_SRC_NRF    0x04 /* last valid command was radio 0xABD0          */
#define PWM_SRC_FAULT  0x06 /* local safety action, e.g. PWM ISR stall     */
#define PWM_SRC_MASK   0x06

static inline uint8_t haus_can_pwm_source(uint8_t flags) {
  return (uint8_t)(flags & PWM_SRC_MASK);
}

typedef struct __attribute__((packed)) {
  uint16_t duty_permille; /* 0 = off, 1000 = full on */
  uint16_t frequency_hz;  /* PWM frequency */
  uint8_t flags;          /* PWM_FLAG_* | PWM_SRC_* */
} haus_can_pwm_t;

static inline void haus_can_set_pwm(uint8_t* buf, const haus_can_pwm_t* p) {
  buf[0] = (uint8_t)(p->duty_permille & 0xFF);
  buf[1] = (uint8_t)(p->duty_permille >> 8);
  buf[2] = (uint8_t)(p->frequency_hz & 0xFF);
  buf[3] = (uint8_t)(p->frequency_hz >> 8);
  buf[4] = p->flags;
  buf[5] = buf[6] = buf[7] = 0;
}

static inline void haus_can_get_pwm(const uint8_t* buf, haus_can_pwm_t* p) {
  p->duty_permille = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
  p->frequency_hz = (uint16_t)buf[2] | ((uint16_t)buf[3] << 8);
  p->flags = buf[4];
}

/* 0x2C0  —  ADC measurement window (raw counts, 1 Hz)
 *
 *  Payload (8 bytes):
 *    [0..1] max count
 *    [2..3] min count
 *    [4..5] peak-to-peak (max - min)
 *    [6]    flags (ADC_FLAG_INTERNAL_REF = 0x01, ADC_FLAG_RAILED = 0x02)
 *    [7]    reserved (zero)
 * ═══════════════════════════════════════════════════════════════════════════
 */
#define CAN_ID_ADC_REPORT 0x2C0

#define ADC_FLAG_INTERNAL_REF 0x01 /* conversion on internal 1.1 V bandgap */
#define ADC_FLAG_RAILED 0x02       /* at least one reading hit full scale */

typedef struct __attribute__((packed)) {
  uint16_t max_count;
  uint16_t min_count;
  uint16_t pp_count; /* peak-to-peak = max - min */
  uint8_t flags;     /* ADC_FLAG_* */
} haus_can_adc_t;

static inline void haus_can_set_adc(uint8_t* buf, const haus_can_adc_t* a) {
  buf[0] = (uint8_t)(a->max_count & 0xFF);
  buf[1] = (uint8_t)(a->max_count >> 8);
  buf[2] = (uint8_t)(a->min_count & 0xFF);
  buf[3] = (uint8_t)(a->min_count >> 8);
  buf[4] = (uint8_t)(a->pp_count & 0xFF);
  buf[5] = (uint8_t)(a->pp_count >> 8);
  buf[6] = a->flags;
  buf[7] = 0;
}

static inline void haus_can_get_adc(const uint8_t* buf, haus_can_adc_t* a) {
  a->max_count = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
  a->min_count = (uint16_t)buf[2] | ((uint16_t)buf[3] << 8);
  a->pp_count = (uint16_t)buf[4] | ((uint16_t)buf[5] << 8);
  a->flags = buf[6];
}

/* 0x300  —  CAN bus health report (slow, ~5 s)
 *
 *  Payload (8 bytes):
 *    [0..1] frames accepted for transmission since boot
 *    [2..3] frames received since boot
 *    [4..5] frames refused (queue full, bad state)
 *    [6]    MCP2515 EFLG (error flags)
 *    [7]    MCP2515 CANSTAT (controller status) | STATS_FLAG_PRESENT
 * ═══════════════════════════════════════════════════════════════════════════
 */
#define CAN_ID_STATS_REPORT 0x300

/* Byte 7 carries the full 8-bit CANSTAT register, leaving exactly one spare
 * bit (0x80) for protocol flags.  Only one flag bit fits on the wire. */
#define STATS_FLAG_PRESENT 0x80   /* MCP2515 answered on SPI bus */
#define STATS_FLAG_NOT_READY 0x00 /* no encoding left — status in CANSTAT */

/* MCP2515 EFLG bits (error flags register 0x2D) */
#define CAN_EFLAG_EWARN 0x01  /* any error counter >= 96 */
#define CAN_EFLAG_RXWAR 0x02  /* receive error warning, REC 96–127 */
#define CAN_EFLAG_TXWAR 0x04  /* transmit error warning, TEC 96–127 */
#define CAN_EFLAG_RXEP 0x08   /* receive error passive, REC >= 128 */
#define CAN_EFLAG_TXEP 0x10   /* transmit error passive, TEC >= 128 */
#define CAN_EFLAG_TXBO 0x20   /* transmit bus-off */
#define CAN_EFLAG_RX0OVR 0x40 /* receive buffer 0 overflow */
#define CAN_EFLAG_RX1OVR 0x80 /* receive buffer 1 overflow */

typedef struct __attribute__((packed)) {
  uint16_t sent;     /* frames accepted for transmission since boot */
  uint16_t received; /* frames taken off the bus since boot */
  uint16_t refused;  /* frames refused (queue full, bad state) */
  uint8_t eflg;      /* MCP2515 error flags (CAN_EFLAG_*) */
  uint8_t canstat;   /* MCP2515 controller status */
  uint8_t flags;     /* STATS_FLAG_* */
} haus_can_stats_t;

static inline void haus_can_set_stats(uint8_t* buf, const haus_can_stats_t* s) {
  buf[0] = (uint8_t)(s->sent & 0xFF);
  buf[1] = (uint8_t)(s->sent >> 8);
  buf[2] = (uint8_t)(s->received & 0xFF);
  buf[3] = (uint8_t)(s->received >> 8);
  buf[4] = (uint8_t)(s->refused & 0xFF);
  buf[5] = (uint8_t)(s->refused >> 8);
  buf[6] = s->eflg;
  buf[7] = s->canstat | s->flags;
}

static inline void haus_can_get_stats(const uint8_t* buf, haus_can_stats_t* s) {
  s->sent = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
  s->received = (uint16_t)buf[2] | ((uint16_t)buf[3] << 8);
  s->refused = (uint16_t)buf[4] | ((uint16_t)buf[5] << 8);
  s->eflg = buf[6];
  s->canstat = buf[7] & 0x7F;
  s->flags = buf[7] & STATS_FLAG_PRESENT;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  0x2C8  —  EEPROM restore-point log status
 *  AVR/01_heizung_temp
 *
 * The log is what makes the commanded duty survive a power cut, so its health is
 *  as much a part of the node's state as the CAN bus's is. Reported separately
 *  from 0x300 because the two change on completely different timescales: CAN
 *  health moves every few seconds, the log moves every eight hours.
 *
 * DLC 8:
 *   [0..1] restored_duty  what the output was set to at boot (uint16 LE)
 *   [2]   flags           EE_FLAG_*, including where the restore came from
 *   [3]   records         valid records in the ring, saturating at 255
 *   [4]   oldest          first slot of the valid arc
 *   [5]   newest          last slot of the valid arc
 *   [6]   writes          appends since boot, saturating at 255
 *   [7]   reserved
 * ═══════════════════════════════════════════════════════════════════════════ */
#define CAN_ID_EE_REPORT 0x2C8

/* Bit 0 is the one that matters most: the ring could not be located, so the
 * output came up on EPL_OFF_DUTY without anything being restored. That is a
 * silent loss of the restore point, and it looks identical to a deliberate off
 * if the duty alone is reported. */
/* Only three flags travel. The rest are derivable from fields already in the
 * same 8-byte frame, and carrying them was not free:
 *
 *   EMPTY   <- records == 0
 *   WRAPPED <- oldest > newest
 *   NOMARK  <- src != EE_SRC_MARKER
 *
 * Carrying six flags plus a three-bit source needs nine bits, and the byte has
 * eight. The overlap landed on bit 5: EE_FLAG_PWMFAIL was 0x20 and
 * EE_SRC_MARKER << EE_SRC_SHIFT is also 0x20. So every restore from a boot
 * marker — the common case — set the PWM-failure bit, and a node that had never
 * failed once reported a fault that outlived its own history. In the other
 * direction the source was unreadable whenever PWMFAIL was set, which is how a
 * normal window-walk restore came to be displayed as an anomaly.
 *
 * Derived, not lost: a decoder that wants them computes them from the frame. */
#define EE_FLAG_ANOMALY 0x01  /* ring unlocatable — restore fell back to OFF */
#define EE_FLAG_BADNEW  0x02  /* newest record failed its checksum           */
/* The log holds a record of a PWM output-path failure. Durable where the
 * escalation latch is not: it survives a power blip, so a node whose output
 * path failed once still says so after its history has been cleared. */
#define EE_FLAG_PWMFAIL 0x04

/* Where the restored duty came from, in the top three bits so it fits the DLC;
 * 0 means "not recorded", which is a legitimate virgin-device answer. Bits 5-7
 * are the source's alone — see the note above. */
#define EE_SRC_SHIFT   5
#define EE_SRC_VIRGIN  0u
#define EE_SRC_MARKER  1u
#define EE_SRC_WINDOW  2u
#define EE_SRC_ANOMALY 3u
#define EE_SRC_INVALID 4u
#define EE_SRC_MASK    0xE0u

typedef struct __attribute__((packed)) {
  uint16_t restored_duty;
  uint8_t flags;
  uint8_t records;
  uint8_t oldest;
  uint8_t newest;
  uint8_t writes;
  uint8_t reserved;
} haus_can_ee_t;

static inline void haus_can_set_ee(uint8_t* buf, const haus_can_ee_t* e) {
  buf[0] = (uint8_t)(e->restored_duty & 0xFF);
  buf[1] = (uint8_t)(e->restored_duty >> 8);
  buf[2] = e->flags;
  buf[3] = e->records;
  buf[4] = e->oldest;
  buf[5] = e->newest;
  buf[6] = e->writes;
  buf[7] = 0;
}

static inline void haus_can_get_ee(const uint8_t* buf, haus_can_ee_t* e) {
  e->restored_duty = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
  e->flags = buf[2];
  e->records = buf[3];
  e->oldest = buf[4];
  e->newest = buf[5];
  e->writes = buf[6];
  e->reserved = 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  haus_protocol_t — enum backed by uint16_t, each value IS the CAN-ID.
 *
 *  No mapping function needed: the enum value IS the CAN-ID.
 *  The CYD monitor simply casts the received CAN-ID to haus_protocol_t
 *  and switches on it — no lookup, no table.
 * ═══════════════════════════════════════════════════════════════════════════
 */
typedef enum : uint16_t {
  HAUS_PROTO_NONE = 0x0000,                          /* unknown / reserved */
  HAUS_PROTO_HEIZUNG_ID = CAN_ID_ID_BASE,            /* 0x180 + rank */
  HAUS_PROTO_HEIZUNG_TEMP = CAN_ID_DATA_BASE,        /* 0x200 */
  HAUS_PROTO_HEIZUNG_PWM = CAN_ID_PWM_SET,           /* 0x240 */
  HAUS_PROTO_HEIZUNG_PWM_RPT = CAN_ID_PWM_REPORT,    /* 0x280 */
  HAUS_PROTO_HEIZUNG_ADC = CAN_ID_ADC_REPORT,        /* 0x2C0 */
  HAUS_PROTO_HEIZUNG_EE = CAN_ID_EE_REPORT,          /* 0x2C8 */
  HAUS_PROTO_HEIZUNG_STATS = CAN_ID_STATS_REPORT,    /* 0x300 */
  HAUS_PROTO_TIMESTAMP = CAN_ID_TIMESTAMP,           /* 0x555 */
  HAUS_PROTO_SML = CAN_ID_STROMZAEHLER_INFO_BASIS,   /* 0x600 */
  HAUS_PROTO_SML_ALT = CAN_ID_STROMZAEHLER_INFO_ALT, /* 0x700 */
} haus_protocol_t;

/* ═══════════════════════════════════════════════════════════════════════════
 *  Helper: get sensor rank from CAN ID (for identity frames)
 *  Returns (uint8_t)-1 if not an identity frame.
 * ═══════════════════════════════════════════════════════════════════════════
 */
static inline uint8_t haus_can_get_rank(uint16_t can_id) {
  if (can_id >= CAN_ID_ID_BASE &&
      can_id < CAN_ID_ID_BASE + HAUS_CAN_MAX_SENSORS)
    return (uint8_t)(can_id - CAN_ID_ID_BASE);
  return (uint8_t)-1;
}

#endif /* HAUS_CAN_H */