/**
 * @file haus_nrf.h
 * @brief nRF24 radio frame definitions, getter and setter functions for all
 *        house-automation AVR projects.
 *
 * Mirrors the CAN protocol (haus_can.h) for the wireless sensor network.
 * Every nRF24 frame carries the same semantic data as its CAN counterpart,
 * but with nRF24-specific framing: 2-byte magic number + CRC-CCITT.
 *
 * All frames are exactly 13 bytes (NRF_FRAME_SIZE) because the RF24 library
 * holds ONE payload length for all pipes — different lengths would corrupt
 * each other's CRC.
 *
 * ─── Radio channels ──────────────────────────────────────────────────────────
 *
 *   Pipe 0 — temperature / PWM / ADC / stats (1 Hz)
 *     magic(2) + payload(9) + crc(2)
 *
 *   Pipe 1 — sensor identity (cyclic, ~1/min + burst at boot)
 *     magic(2) + rank(1) + rom(8) + crc(2)
 *
 * Two independent radio channels, separated by nRF24 pipe. Pipes 1–5 share
 * their first 32 address bytes, so the last byte selects the channel.
 *
 * ─── Usage ───────────────────────────────────────────────────────────────────
 *
 *   AVR projects:  #include "haus_nrf.h"  (in AVR/include/)
 *
 * Both paths are symlinks to the single source of truth.
 */

#ifndef HAUS_NRF_H
#define HAUS_NRF_H

#include <stdint.h>
#include <Arduino.h>
#include "haus_can.h"

/* ═══════════════════════════════════════════════════════════════════════════
 *  Protocol constants
 * ═══════════════════════════════════════════════════════════════════════════ */
static const uint16_t NRF_MAGIC_TEMP    = 0xABCD;  /* temperature frame */
static const uint16_t NRF_MAGIC_ID      = 0xABCE;  /* identity frame */
static const uint16_t NRF_MAGIC_PWM     = 0xABCF;  /* PWM output state */
static const uint16_t NRF_MAGIC_PWM_CMD = 0xABD0;  /* duty command to TX */
static const uint16_t NRF_MAGIC_ADC     = 0xABD1;  /* ADC window */
static const uint16_t NRF_MAGIC_STATS   = 0xABD2;  /* CAN bus health */
static const int16_t  NRF_ERR_FLAG      = INT16_MIN;  /* no sensor in slot */
static const uint8_t  NRF_MAX_SENSORS  = HAUS_CAN_MAX_SENSORS;

static const uint8_t NRF_FRAME_SIZE = 13;  /* all frames exactly 13 bytes */

/* ═══════════════════════════════════════════════════════════════════════════
 *  Pipe 0: temperature frame (13 bytes, packed)
 *
 *  [0..1]  magic (NRF_MAGIC_TEMP)
 *  [2..9]  temp[NRF_MAX_SENSORS] (4 × int16_t, c° = °C × 100)
 *  [10]    present bitmask (bit N set → slot N holds a sensor)
 *  [11..12] CRC-CCITT over all preceding fields
 * ═══════════════════════════════════════════════════════════════════════════ */
typedef struct __attribute__((packed)) {
    uint16_t magic;                     /* NRF_MAGIC_TEMP */
    int16_t  temp[NRF_MAX_SENSORS];     /* c° (°C × 100), ERR_FLAG if empty */
    uint8_t  present;                   /* bit N set → slot N holds a sensor */
    uint16_t crc;                       /* CRC-CCITT over preceding fields */
} haus_nrf_temp_frame_t;

/* ═══════════════════════════════════════════════════════════════════════════
 *  Pipe 1: identity frame (13 bytes, packed)
 *
 *  [0..1]  magic (NRF_MAGIC_ID)
 *  [2]     rank (0..3, from ROM sort)
 *  [3..10] rom[8] (DS18B20: family 0x28 + 6-byte serial)
 *  [11..12] CRC-CCITT
 * ═══════════════════════════════════════════════════════════════════════════ */
typedef struct __attribute__((packed)) {
    uint16_t magic;         /* NRF_MAGIC_ID */
    uint8_t  rank;          /* 0..3, from ROM sort */
    uint8_t  rom[8];        /* DS18B20 ROM: family + 6-byte serial */
    uint16_t crc;           /* CRC-CCITT over preceding fields */
} haus_nrf_id_frame_t;

/* ═══════════════════════════════════════════════════════════════════════════
 *  PWM output state frame (13 bytes, packed)
 *
 *  [0..1]  magic (NRF_MAGIC_PWM or NRF_MAGIC_PWM_CMD)
 *  [2..3]  duty in permille (0–1000)
 *  [4..5]  frequency in Hz
 *  [6]     flags (PWM_FLAG_ACTIVE while modulating)
 *  [7..10] reserved (zero)
 *  [11..12] CRC-CCITT
 * ═══════════════════════════════════════════════════════════════════════════ */
typedef struct __attribute__((packed)) {
    uint16_t magic;                  /* NRF_MAGIC_PWM or NRF_MAGIC_PWM_CMD */
    uint16_t duty_permille;          /* 0 = off, 1000 = full on */
    uint16_t frequency_hz;           /* PWM frequency */
    uint8_t  flags;                  /* PWM_FLAG_ACTIVE */
    uint8_t  reserved[4];            /* reserved */
    uint16_t crc;                    /* CRC-CCITT over preceding fields */
} haus_nrf_pwm_frame_t;

/* ═══════════════════════════════════════════════════════════════════════════
 *  ADC window frame (13 bytes, packed)
 *
 *  [0..1]  magic (NRF_MAGIC_ADC)
 *  [2..3]  max count
 *  [4..5]  min count
 *  [6..7]  peak-to-peak (max - min)
 *  [8..9]  readings folded in (saturates at 0xFFFF)
 *  [10]    flags (ADC_FLAG_INTERNAL_REF, ADC_FLAG_RAILED)
 *  [11..12] CRC-CCITT
 * ═══════════════════════════════════════════════════════════════════════════ */
typedef struct __attribute__((packed)) {
    uint16_t magic;      /* NRF_MAGIC_ADC */
    uint16_t max_count;  /* largest conversion seen in the window */
    uint16_t min_count;  /* smallest conversion seen in the window */
    uint16_t pp_count;   /* peak-to-peak = max - min */
    uint16_t readings;   /* readings folded in; saturates at 0xFFFF */
    uint8_t  flags;      /* ADC_FLAG_* */
    uint16_t crc;        /* CRC-CCITT over preceding fields */
} haus_nrf_adc_frame_t;

/* ═══════════════════════════════════════════════════════════════════════════
 *  CAN bus health frame (13 bytes, packed)
 *
 *  [0..1]  magic (NRF_MAGIC_STATS)
 *  [2..3]  frames accepted for transmission since boot
 *  [4..5]  frames received since boot
 *  [6..7]  frames refused (queue full, bad state)
 *  [8]     MCP2515 EFLG (error flags)
 *  [9]     MCP2515 CANSTAT (controller status)
 *  [10]    STATS_FLAG_* (present, not ready)
 *  [11..12] CRC-CCITT
 * ═══════════════════════════════════════════════════════════════════════════ */
typedef struct __attribute__((packed)) {
    uint16_t magic;      /* NRF_MAGIC_STATS */
    uint16_t sent;       /* frames accepted for transmission since boot */
    uint16_t received;   /* frames taken off the bus since boot */
    uint16_t refused;    /* frames refused (queue full, bad state) */
    uint8_t  eflg;       /* MCP2515 error flags (CAN_EFLAG_*) */
    uint8_t  canstat;    /* MCP2515 controller status */
    uint8_t  flags;      /* STATS_FLAG_* */
    uint16_t crc;        /* CRC-CCITT over preceding fields */
} haus_nrf_stats_frame_t;

/* ─── Flags (shared with haus_can.h) ─────────────────────────────────────── */
#define PWM_FLAG_ACTIVE  0x01  /* 0 < duty < 1000 permille */
#define ADC_FLAG_INTERNAL_REF 0x01  /* conversion on internal 1.1 V bandgap */
#define ADC_FLAG_RAILED       0x02  /* at least one reading hit full scale */
#define STATS_FLAG_PRESENT    0x01  /* MCP2515 answered on SPI bus */
#define STATS_FLAG_NOT_READY  0x02  /* controller not yet initialised */

#define TEMP_PRESENT_BIT(i) (1 << (i))

/* ═══════════════════════════════════════════════════════════════════════════
 *  Compile-time checks: all frames must be exactly NRF_FRAME_SIZE bytes
 * ═══════════════════════════════════════════════════════════════════════════ */
static_assert(sizeof(haus_nrf_temp_frame_t) == NRF_FRAME_SIZE,
              "haus_nrf_temp_frame_t size drift");
static_assert(sizeof(haus_nrf_id_frame_t)   == NRF_FRAME_SIZE,
              "haus_nrf_id_frame_t size drift");
static_assert(sizeof(haus_nrf_pwm_frame_t)  == NRF_FRAME_SIZE,
              "haus_nrf_pwm_frame_t size drift");
static_assert(sizeof(haus_nrf_adc_frame_t)  == NRF_FRAME_SIZE,
              "haus_nrf_adc_frame_t size drift");
static_assert(sizeof(haus_nrf_stats_frame_t)== NRF_FRAME_SIZE,
              "haus_nrf_stats_frame_t size drift");

/* ═══════════════════════════════════════════════════════════════════════════
 *  Getter and setter functions — nRF24 frames
 *
 *  These convert between raw bytes (for RF24 transmission) and semantic
 *  structs (for application logic). The semantic structs match haus_can.h
 *  so data can be translated between radio and CAN with one conversion.
 * ═══════════════════════════════════════════════════════════════════════════ */

/* ─── Temperature ─────────────────────────────────────────────────────────── */
static inline void haus_nrf_set_temp(haus_nrf_temp_frame_t *frame,
                                      const haus_can_temperature_t *data)
{
    frame->magic = NRF_MAGIC_TEMP;
    for (int i = 0; i < NRF_MAX_SENSORS; i++)
        frame->temp[i] = data->temperature[i];
    /* present bitmask: caller sets this, or use TEMP_PRESENT_BIT(i) */
    frame->present = 0;  /* caller sets presence bits */
    frame->crc = 0;  /* computed by frame_crc16() below */
}

static inline void haus_nrf_get_temp(const haus_nrf_temp_frame_t *frame,
                                      haus_can_temperature_t *data)
{
    for (int i = 0; i < NRF_MAX_SENSORS; i++)
        data->temperature[i] = frame->temp[i];
    /* caller reads frame->present for which slots are valid */
}

/* ─── Identity ────────────────────────────────────────────────────────────── */
static inline void haus_nrf_set_id(haus_nrf_id_frame_t *frame,
                                    uint8_t rank, const uint8_t rom[8])
{
    frame->magic = NRF_MAGIC_ID;
    frame->rank = rank;
    memcpy(frame->rom, rom, 8);
    frame->crc = 0;  /* computed by frame_crc16() below */
}

static inline void haus_nrf_get_id(const haus_nrf_id_frame_t *frame,
                                    uint8_t *rank, uint8_t rom[8])
{
    if (rank != NULL) *rank = frame->rank;
    if (rom != NULL)  memcpy(rom, frame->rom, 8);
}

/* ─── PWM ─────────────────────────────────────────────────────────────────── */
static inline void haus_nrf_set_pwm(haus_nrf_pwm_frame_t *frame,
                                     uint16_t magic,
                                     const haus_can_pwm_t *data)
{
    frame->magic = magic;  /* NRF_MAGIC_PWM or NRF_MAGIC_PWM_CMD */
    frame->duty_permille = data->duty_permille;
    frame->frequency_hz  = data->frequency_hz;
    frame->flags         = data->flags;
    memset(frame->reserved, 0, sizeof(frame->reserved));
    frame->crc = 0;  /* computed by frame_crc16() below */
}

static inline void haus_nrf_get_pwm(const haus_nrf_pwm_frame_t *frame,
                                     haus_can_pwm_t *data)
{
    data->duty_permille = frame->duty_permille;
    data->frequency_hz  = frame->frequency_hz;
    data->flags         = frame->flags;
}

/* ─── ADC ─────────────────────────────────────────────────────────────────── */
static inline void haus_nrf_set_adc(haus_nrf_adc_frame_t *frame,
                                     const haus_can_adc_t *data)
{
    frame->magic = NRF_MAGIC_ADC;
    frame->max_count = data->max_count;
    frame->min_count = data->min_count;
    frame->pp_count  = data->pp_count;
    frame->readings  = 0;  /* caller sets */
    frame->flags     = data->flags;
    frame->crc = 0;  /* computed by frame_crc16() below */
}

static inline void haus_nrf_get_adc(const haus_nrf_adc_frame_t *frame,
                                     haus_can_adc_t *data)
{
    data->max_count = frame->max_count;
    data->min_count = frame->min_count;
    data->pp_count  = frame->pp_count;
    data->flags     = frame->flags;
}

/* ─── Stats ───────────────────────────────────────────────────────────────── */
static inline void haus_nrf_set_stats(haus_nrf_stats_frame_t *frame,
                                       const haus_can_stats_t *data)
{
    frame->magic = NRF_MAGIC_STATS;
    frame->sent      = data->sent;
    frame->received  = data->received;
    frame->refused   = data->refused;
    frame->eflg      = data->eflg;
    frame->canstat   = data->canstat;
    frame->flags     = data->flags;
    frame->crc = 0;  /* computed by frame_crc16() below */
}

static inline void haus_nrf_get_stats(const haus_nrf_stats_frame_t *frame,
                                       haus_can_stats_t *data)
{
    data->sent      = frame->sent;
    data->received  = frame->received;
    data->refused   = frame->refused;
    data->eflg      = frame->eflg;
    data->canstat   = frame->canstat;
    data->flags     = frame->flags;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  CRC-CCITT (0xFFFF init, poly 0x1021)
 * ═══════════════════════════════════════════════════════════════════════════ */
static inline uint16_t nrf_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc = crc << 1;
        }
    }
    return crc;
}

/* CRC over every field before the trailing crc (all frames end in crc) */
static inline uint16_t haus_nrf_frame_crc16(const void *frame, uint8_t frameSize)
{
    return nrf_crc16((const uint8_t *)frame, (uint16_t)(frameSize - sizeof(uint16_t)));
}

/* Compute CRC for a specific frame type (caller sets crc = 0 first) */
static inline uint16_t haus_nrf_compute_crc(const haus_nrf_temp_frame_t *f)
{
    (void)f; return 0;  /* caller uses haus_nrf_frame_crc16() */
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  WiFi coexistence helpers (from nrf_payload.h)
 * ═══════════════════════════════════════════════════════════════════════════ */
static const uint8_t NRF_CHANNEL = 80;  /* safest channel — above all WiFi */

/* ─── Addressing ──────────────────────────────────────────────────────────── */
static const uint8_t NRF_BC_ADDR[5]  = { 0x00, 0xCD, 0x12, 0x34, 0x56 };  /* broadcasts */
static const uint8_t NRF_CMD_ADDR[5] = { 0x01, 0xCD, 0x12, 0x34, 0x56 };  /* commands to TX */

/* ─── Format c° (int16_t) as "X.XX" string — integer-only, no float ───────── */
static inline String haus_nrf_cdeg_to_str(int16_t cdeg)
{
    if (cdeg == NRF_ERR_FLAG) return F("N/A");

    bool neg = (cdeg < 0);
    uint16_t abs_val = (uint16_t)(neg ? -cdeg : cdeg);

    char buf[10];
    uint8_t idx = 0;
    uint16_t tmp = abs_val;
    do { buf[idx++] = (tmp % 10) + '0'; tmp /= 10; } while (tmp > 0);
    for (uint8_t i = 0; i < idx / 2; i++) {
        char sw = buf[i]; buf[i] = buf[idx - 1 - i]; buf[idx - 1 - i] = sw;
    }
    while (idx < 3) buf[idx++] = '0';
    buf[idx] = buf[idx - 1];
    buf[idx - 1] = buf[idx - 2];
    buf[idx - 2] = '.';
    idx++;
    buf[idx] = '\0';
    if (neg) {
        buf[idx] = buf[0];
        buf[0] = '-';
        idx++;
        buf[idx] = '\0';
    }

    return String(buf);
}

/* ─── Print ROM address as 16 hex chars ───────────────────────────────────── */
static inline String haus_nrf_rom_to_str(const uint8_t rom[8])
{
    char buf[17];
    for (uint8_t i = 0; i < 8; i++) {
        uint8_t hi = (rom[i] >> 4) & 0x0F;
        uint8_t lo = rom[i] & 0x0F;
        buf[i * 2]     = (hi < 10) ? hi + '0' : hi + 'A' - 10;
        buf[i * 2 + 1] = (lo < 10) ? lo + '0' : lo + 'A' - 10;
    }
    buf[16] = '\0';
    return String(buf);
}

#endif /* HAUS_NRF_H */