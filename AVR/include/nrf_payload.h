/**
 * @file nrf_payload.h
 * @brief Shared frame definitions for 01_heizung_temp and 02_nrf_receiver
 *
 * Two independent radio channels, separated by nRF24 pipe. Pipes 1–5 share
 * their first 32 address bytes, so the last byte selects the channel:
 *
 *   Pipe 0 — temperature frame, 12 bytes (1 Hz)
 *     magic(2) + temp[4](8) + crc(2)
 *
 *   Pipe 1 — identity frame, 13 bytes (cyclic, ~1/min + burst at boot)
 *     magic(2) + rank(1) + rom(8) + crc(2)
 *
 * Every frame is 13 bytes because the radio holds ONE payload length for all
 * pipes — see the static_asserts below. Types are distinguished by magic.
 *
 * Identity is deliberately NOT part of the data frame. A DS18B20 ROM is 8
 * bytes, so four of them plus framing would exceed the nRF24L01+ 32-byte
 * payload limit, and the ROMs never change — re-sending them every second
 * would buy nothing. Splitting them also means the fast frame shrank from
 * 20 bytes to 12.
 *
 * Because identity repeats indefinitely, a receiver that powers up at any
 * moment converges on the full sensor set with no handshake and no ordering
 * requirement: it simply waits for the announcements it has not seen yet.
 *
 * Rank is assigned by sorting the present sensors by ROM address, and is the
 * value mapped onto downstream identifiers (see the CAN block at the bottom).
 */

#ifndef NRF_PAYLOAD_H
#define NRF_PAYLOAD_H

#include <stdint.h>
#include <Arduino.h>
#include "haus_can.h"  // CAN IDs, flags and MCP2515 error bits live here now

// ─── Protocol constants ──────────────────────────────────────────────────────
static const uint16_t NRF_MAGIC_TEMP = 0xABCD;  // temperature frame
static const uint16_t NRF_MAGIC_ID   = 0xABCE;  // identity frame
static const uint16_t NRF_MAGIC_PWM  = 0xABCF;  // PWM output state
static const uint16_t NRF_MAGIC_PWM_CMD = 0xABD0;  // duty command, sent to the TX
static const uint16_t NRF_MAGIC_ADC    = 0xABD1;  // ADC peak-to-peak window
static const uint16_t NRF_MAGIC_STATS  = 0xABD2;  // CAN bus health
static const uint16_t NRF_MAGIC_EE     = 0xABD3;  // EEPROM restore-point log status
static const uint16_t NRF_MAGIC_SYS    = 0xABD4;  // diagnostic command, receiver -> transmitter
static const int16_t  ERR_FLAG       = -32768;  // INT16_MIN — no sensor in slot
static const uint8_t  NRF_MAX_SENSORS = 4;

// ─── Pipe 0: temperature frame (13 bytes, packed) ───────────────────────────
// Deliberately the same size as IdFrame. RF24 holds ONE payload length for the
// whole radio (RF24::payload_size); with static payloads write_payload() pads
// every frame out to it, so two pipes carrying different lengths would corrupt
// each other's CRC. The byte that padding would have cost carries the presence
// bitmask instead.
struct __attribute__((packed)) TempFrame {
    uint16_t magic;                     // NRF_MAGIC_TEMP
    int16_t  temp[NRF_MAX_SENSORS];     // c° (°C × 100), ERR_FLAG if slot empty
    uint8_t  present;                   // bit N set → slot N holds a sensor
    uint16_t crc;                       // CRC-CCITT over all preceding fields
};

// ─── Pipe 1: identity frame (13 bytes, packed) ───────────────────────────────
struct __attribute__((packed)) IdFrame {
    uint16_t magic;         // NRF_MAGIC_ID
    uint8_t  rank;          // 0..3, from ROM sort — the stable sensor identity
    uint8_t  rom[8];        // full DS18B20 ROM: family(0x28) + 6-byte serial + CRC
    uint16_t crc;           // CRC-CCITT over all preceding fields
};

// ─── PWM output state frame (13 bytes, packed) ───────────────────────────────
// Reports the commanded duty so a remote receiver can mirror it. The duty is
// commanded in from CAN and echoed out over both radios.
struct __attribute__((packed)) PwmFrame {
    uint16_t magic;                  // NRF_MAGIC_PWM
    uint16_t duty;                   // commanded duty in permille (0…1000)
    uint16_t hz;                     // PWM frequency in Hz
    uint8_t  flags;                  // PWM_FLAG_ACTIVE (haus_can.h) while running
    uint8_t  reserved[4];
    uint16_t crc;                    // CRC-CCITT over all preceding fields
};

// ─── Pipe 0: ADC window frame (13 bytes, packed) ─────────────────────────────
// Peak-to-peak over one measurement window. All three values are raw ADC counts
// — no scaling is applied, because the reference is external and the conversion
// that produced these cannot know its own accuracy. Scaling belongs to the
// consumer, which knows what it put on AREF.
struct __attribute__((packed)) AdcFrame {
    uint16_t magic;      // NRF_MAGIC_ADC
    uint16_t max;        // largest conversion seen in the window
    uint16_t min;        // smallest conversion seen in the window
    uint16_t pp;         // max - min, the peak-to-peak span
    uint16_t readings;   // readings folded in; saturates at 0xFFFF
    uint8_t  flags;      // ADC_FLAG_* (haus_can.h)
    uint16_t crc;        // CRC-CCITT over all preceding fields
};

// ─── Pipe 0: EEPROM restore-point log status (13 bytes, packed) ─────────────
//
// Separate from the CAN-health frame because the two move on completely
// different timescales: bus health changes every few seconds, the log every
// eight hours. A receiver watching this must be able to tell "the duty came up
// off because that was the decision" from "the duty came up off because the log
// was unreadable" — which is why the restore source travels with the value.
struct __attribute__((packed)) EeFrame {
    uint16_t magic;        // NRF_MAGIC_EE
    uint16_t restored;     // duty the output was set to at boot, 0xFFFF = none
    uint16_t records;      // valid records in the ring
    uint16_t writes;       // appends since boot
    uint8_t  flags;        // EE_FLAG_*, incl. the restore source in bits 5..7
    uint8_t  oldest;       // first slot of the valid arc
    uint8_t  newest;       // last slot of the valid arc
    uint16_t crc;          // CRC-CCITT over all preceding fields
};

// ─── Pipe 1: diagnostic command (13 bytes, packed) ──────────────────────────
//
// Bench-only: these drive failure paths that cannot be produced any other way,
// the reboot and the PWM-output-path escalation in particular. The escalation
// ends in a watchdog reset, so it is otherwise unobservable from a test bench.
//
// This adds no new exposure. The same pipe already accepts a duty command, which
// controls the heater directly; a command that reboots the node is strictly
// less consequential than one that sets its output. Every one of these is
// logged loudly by the transmitter when it acts.
struct __attribute__((packed)) SysCmdFrame {
    uint16_t magic;         // NRF_MAGIC_SYS
    uint8_t  cmd;           // SYSCMD_*
    uint8_t  arg;
    uint8_t  reserved[7];
    uint16_t crc;
};

#define SYSCMD_REBOOT      0  /* starve the watchdog: force a reset          */
#define SYSCMD_PWM_STALL   1  /* clobber TIMSK1 to simulate a dead PWM path */
#define SYSCMD_EE_ANOMALY  2  /* fill every slot: force the unlocatable ring */
#define SYSCMD_CLEAR_STATS 3  /* zero the RAM counters (does NOT touch EEPROM) */
#define SYSCMD_REVIVE      4  /* leave failover and retry the timer output path */
#define SYSCMD_EE_ERASE    5  /* erase the whole log: back to virgin         */

/* E and A are deliberately different operations and were once conflated into one
 * command named "anomaly", which erased the log to 0xFF and therefore produced
 * the VIRGIN path rather than the anomaly path it claimed to exercise. They are
 * not the same thing:
 *
 *   erase   -> every slot 0xFF  -> no valid records at all -> virgin, restore
 *              falls back to OFF. This is the operator action.
 *   anomaly -> every slot holds a *valid* record -> no empty arc exists -> the
 *              newest cannot be located. This is the fault injection, and it is
 *              the case that must never return a random duty. */

// ─── Pipe 0: CAN bus health frame (13 bytes, packed) ─────────────────────────
// Publishes the CAN side of the system over the radio so it can be watched
// without a scope or a CAN sniffer on the bus. Reported on its own slower
// cadence than the ADC: health changes slowly, and the numbers are far easier to
// read on a 5 s beat than every second.
struct __attribute__((packed)) StatsFrame {
    uint16_t magic;      // NRF_MAGIC_STATS
    uint16_t sent;       // frames accepted for transmission since boot
    uint16_t received;   // frames read from the RX buffers (heater: 0x240 only)
    uint16_t refused;    // frames this board would not transmit (queue full, ...)
    uint8_t  eflg;       // MCP2515 EFLG, error flags as read
    uint8_t  canstat;    // MCP2515 CANSTAT, controller status as read
    uint8_t  present;    // NRF_STATS_FLAG_* — is there a controller at all
    uint16_t crc;        // CRC-CCITT over all preceding fields
};

// Radio-side presence bits for StatsFrame.present.  These are distinct from the
// CAN-wire STATS_FLAG_* in haus_can.h, which share a byte with the MCP2515
// CANSTAT register and therefore use different bit values.
//
// Whether an MCP2515 answered on the SPI bus and was initialised successfully.
//
// This is the bit that separates "the bus is broken" from "there is no bus". With
// the controller absent, can_sender is deliberately fail-soft: every send becomes
// a no-op and the radio and temperature paths keep running, which is correct for
// an unpopulated board but leaves all the other counters reading zero — the same
// numbers a perfectly healthy idle bus produces. A receiver watching wirelessly
// cannot tell those two states apart without this flag.
//
// Cleared when can_init() found no controller; set once initialisation succeeds,
// and never cleared again.
#define NRF_STATS_FLAG_PRESENT 0x01

// The module is not ready: either never initialised, or a frame was attempted
// before can_init() returned. Distinct from PRESENT being clear — one means "no
// hardware", this means "not yet up".
#define NRF_STATS_FLAG_NOT_READY 0x02

// The restore-point log could not be located, so the duty came up on the boot
// constant with nothing restored. Latched here as well as carried in detail by
// the 0xABD3 frame, because an anomaly is the single most important thing to
// know about this node and it should be visible without decoding a second frame.
#define NRF_STATS_FLAG_EE_ANOMALY 0x04

// MCP2515 EFLG bits (CAN_EFLAG_*) are defined in haus_can.h.
#define CAN_EFLAG_MASK  0xFF

// CANSTAT bits, MCP2515 (register 0x0E).
//
// The four macros below are NOT MCP2515 CANSTAT bits — they are the bit masks
// from the classic AVR on-chip CAN (ATmega8/16/32).  They are left in place
// only so older reader firmware that still references them continues to compile;
// they are unused by this firmware.
//
// Real CANSTAT bits:
//   bits 7..5  OPMOD  — operation mode (0=normal 1=sleep 2=loopback
//                          3=listen-only 4=config)
//   bits 3..1  ICOD   — interrupt code
//   bit 0      TXEP   — transmit error passive flag
#define CANSTAT_TXREQ  0x04  // transmit request pending — a frame has not been ACKed
#define CANSTAT_RXERR  0x08  // receive error
#define CANSTAT_TXERR  0x10  // transmit error
#define CANSTAT_TXWARN 0x20  // transmit error warning, TXERR 96–127

#define TEMP_PRESENT_BIT(i) (1 << (i))

static const uint8_t NRF_FRAME_SIZE = sizeof(TempFrame);    // 13

// RF24 pads or truncates every transmission to one global payload length, so
// all frame types must be exactly the same size or they corrupt each other.
static_assert(sizeof(TempFrame) == NRF_FRAME_SIZE, "TempFrame size drift");
static_assert(sizeof(IdFrame)   == NRF_FRAME_SIZE, "IdFrame size drift");
static_assert(sizeof(PwmFrame)  == NRF_FRAME_SIZE, "PwmFrame size drift");
static_assert(sizeof(AdcFrame)  == NRF_FRAME_SIZE, "AdcFrame size drift");
static_assert(sizeof(StatsFrame)== NRF_FRAME_SIZE, "StatsFrame size drift");
static_assert(sizeof(EeFrame)   == NRF_FRAME_SIZE, "EeFrame size drift");
static_assert(sizeof(SysCmdFrame)== NRF_FRAME_SIZE, "SysCmdFrame size drift");

// ─── Downstream CAN mapping ──────────────────────────────────────────────────
// Classic CAN carries at most 8 data bytes, which is exactly one DS18B20 ROM
// and exactly four centidegree readings. No software CRC is added: the CAN
// frame already has hardware CRC-16, bit stuffing and CRC-error detection.
//
// The CAN identifiers (CAN_ID_ID_BASE, CAN_ID_DATA_BASE, CAN_ID_PWM_SET,
// CAN_ID_PWM_REPORT, CAN_ID_ADC_REPORT, CAN_ID_STATS_REPORT) live in haus_can.h,
// which is included at the top of this file.

// ─── CRC-CCITT (0xFFFF init, poly 0x1021) ────────────────────────────────────
static inline uint16_t nrf_crc16(const uint8_t* data, uint16_t len)
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

// ─── CRC over every field before the trailing crc (both frames end in crc) ───
static inline uint16_t frame_crc16(const void* frame, uint8_t frameSize)
{
    return nrf_crc16((const uint8_t*)frame, (uint16_t)(frameSize - sizeof(uint16_t)));
}

// ─── Format c° (int16_t) as "X.XX" string — integer-only, no float ──────────
static inline String cdeg_to_str(int16_t cdeg)
{
    if (cdeg == ERR_FLAG) return F("N/A");

    bool neg = (cdeg < 0);
    uint16_t abs_val = (uint16_t)(neg ? -cdeg : cdeg);

    // Convert to string (e.g. 2575 → "2575"). buf is sized for the widest
    // int16 (5 digits) plus sign, decimal point and terminator.
    char buf[10];
    uint8_t idx = 0;
    uint16_t tmp = abs_val;
    do { buf[idx++] = (tmp % 10) + '0'; tmp /= 10; } while (tmp > 0);
    // reverse
    for (uint8_t i = 0; i < idx / 2; i++) {
        char sw = buf[i]; buf[i] = buf[idx - 1 - i]; buf[idx - 1 - i] = sw;
    }

    // Pad left with '0' until length >= 3
    while (idx < 3) buf[idx++] = '0';

    // Insert '.' at second position from right
    buf[idx] = buf[idx - 1];
    buf[idx - 1] = buf[idx - 2];
    buf[idx - 2] = '.';
    idx++;
    buf[idx] = '\0';

    // Prepend sign if negative. The terminator has to be re-placed afterwards:
    // the sign is written into the slot the terminator occupied, so without
    // this String(buf) runs off the end and prints stack garbage.
    if (neg) {
        buf[idx] = buf[0];
        buf[0] = '-';
        idx++;
        buf[idx] = '\0';
    }

    return String(buf);
}

// ─── WiFi coexistence helpers ────────────────────────────────────────────────
// nRF24 channels 0–79 map to 2400–2479 MHz (1 MHz spacing)
// WiFi channels (802.11b/g/n) are ~22 MHz wide, centered 5 MHz apart:
//   Ch 1: 2412 MHz (span ~2401–2423)
//   Ch 6: 2437 MHz (span ~2426–2448)
//   Ch 11: 2462 MHz (span ~2451–2473)
//
// Safe zones (no WiFi overlap):
//   nRF24 ch  0–11  → 2400–2411 MHz  (below WiFi ch 1)
//   nRF24 ch 74–79  → 2474–2479 MHz  (above WiFi ch 11)
//
// Channel 80 (=2480 MHz) is the safest single channel — above all WiFi.
// If you must move, prefer ch 74–79 or ch 0–11.
//
// AVOID these WiFi-collision zones:
//   nRF24 ch 12–23  (WiFi ch 1 overlap)
//   nRF24 ch 32–47  (WiFi ch 6 overlap)
//   nRF24 ch 51–63  (WiFi ch 11 overlap)
static const uint8_t NRF_CHANNEL = 80;

// ─── Addressing ─────────────────────────────────────────────────────────────
// Only the first five bytes of an address array reach the radio, and RF24
// pushes the array LSB-first — so array[0] is the LAST byte on the air, which
// is exactly the byte the nRF24L01+ uses to discriminate pipes 1-5. A sixth
// "selector" byte in the array is silently discarded, which is what silently
// collapsed an earlier two-pipe attempt onto one address.
//
// Consequence: pipes 1-5 must share array[1..4] and vary array[0].
static const uint8_t NRF_BC_ADDR[5]  = { 0x00, 0xCD, 0x12, 0x34, 0x56 };  // broadcasts
static const uint8_t NRF_CMD_ADDR[5] = { 0x01, 0xCD, 0x12, 0x34, 0x56 };  // commands to the TX

// ─── Print ROM address as 16 hex chars ───────────────────────────────────────
static inline String rom_to_str(const uint8_t rom[8])
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

#endif // NRF_PAYLOAD_H