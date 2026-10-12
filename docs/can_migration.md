# CAN Protocol Migration: haus_can.h

## Overview

This document describes the migration from project-specific CAN header files to
the unified `haus_can.h` interface.  All 125 kbps CAN bus projects now share
single definitions for CAN identifiers, message structures, and encoding/decoding
helpers.

The single source of truth lives at `<repo>/include/haus_can.h`.  It is wired
into the builds by single-hop symlinks:

- ESP32: `ln -sf ../../../include/* .` in each `<project>/include/` (via
  `make links` in `ESP32/`)
- AVR: `<repo>/AVR/include/haus_can.h` → `../../include/haus_can.h`, reached
  via `-I../include`

## Files Changed

| Project | Old File | New Include | Status |
|---------|----------|-------------|--------|
| AVR/07_stromzaehler | hard-coded `0x600` | `haus_can.h` | ✅ Migrated |
| AVR/01_heizung_temp | `can_sender.h` IDs | `haus_can.h` + `can_sender.h` | ✅ Migrated |
| ESP32/15_power_log_eink | `can_ids.h` | `haus_can.h` | ✅ Migrated |
| ESP32/16_main_control | `can_ids.h` | `haus_can.h` | ✅ Migrated |
| ESP32/26_CYD_CAN_monitor | `can_ids.h` | `haus_can.h` | ✅ Migrated |

## Per-Project Changes

### AVR/07_stromzaehler (stromzaehler.cpp)

**Before:** Hard-coded `uint16_t base_id = 0x600;` with `base_id ^= 0x100`
toggling between 0x600 and 0x700.

**After:** Uses `CAN_ID_STROMZAEHLER_INFO_BASIS` and
`CAN_ID_STROMZAEHLER_INFO_ALT` from `haus_can.h`.  The toggle logic is now
explicit:

```cpp
base_id = (base_id == CAN_ID_STROMZAEHLER_INFO_BASIS)
              ? CAN_ID_STROMZAEHLER_INFO_ALT
              : CAN_ID_STROMZAEHLER_INFO_BASIS;
```

### AVR/01_heizung_temp (can_sender.cpp + main.cpp)

**Before:** `can_send_*()` functions manually packed data bytes into 8-byte
buffers with hand-written byte shifts.

**After:** Each `can_send_*()` function now calls the corresponding
`haus_can_set_*()` helper from `haus_can.h` to pack the data, then submits
through the existing MCP2515 layer.  The external API (`can_send_identity()`,
etc.) is unchanged — callers need no changes to their call sites.  `main.cpp`
itself did change, to add the `#include "haus_can.h"` line.

**Files changed:**
- `can_sender.cpp`: Added `#include "haus_can.h"`, rewrote all `can_send_*()`
  functions to use `haus_can_set_*()` helpers.
- `can_sender.h`: Added `#include "haus_can.h"` so the CAN_ID macros are
  available to all consumers.
- `main.cpp`: Added `#include "haus_can.h"` alongside `can_sender.h`.

### ESP32/15_power_log_eink (main.cpp)

**Before:** `#include "can_ids.h"` — a small header defining only
`CAN_ID_TIMESTAMP` (0x555) and `CAN_ID_STROMZAEHLER_INFO_BASIS` (0x600).

**After:** `#include "haus_can.h"` — gains all protocol IDs, all structures,
and all inline encoding/decoding helpers.

### ESP32/16_main_control (main.cpp)

**Before:** `#include "can_ids.h"` — same minimal header.

**After:** `#include "haus_can.h"` — same benefit as 15_power_log_eink.

### ESP32/26_CYD_CAN_monitor (main.cpp) — Largest change

**Before:** Local `enum can_protocol_t` with hand-mapped values (0, 1, 2, 3)
and a `decode_protocol()` function that checked IDs with `if`/`else` chains.

**After:** Uses `haus_protocol_t` from `haus_can.h` where **each enum value IS
the CAN-ID**.  The `decode_protocol()` function now:

1. Casts the received CAN-ID directly to `haus_protocol_t`
2. Switches on the enum value — no mapping table needed
3. Handles the identity range `0x180 .. 0x183` (one per sensor rank) with
   `haus_can_get_rank()` *before* the switch, then decodes temperature, PWM
   (command `0x240` and report `0x280`), ADC, and stats using the
   `haus_can_get_*()` helpers
4. Preserves existing SML (both `0x600` and `0x700`) and time-broadcast handling
5. Tags `info->protocol` so the UI color-codes each protocol

**New protocol decoding:**

| CAN ID | Protocol | Label | Color |
|--------|----------|-------|-------|
| 0x180–0x183 | Sensor identity | `ID 0x180+N` | Yellow |
| 0x200 | Temperature | `TEMP` | Green |
| 0x240 | PWM command | `PWM` | Magenta |
| 0x280 | PWM report | `PWM` | Magenta |
| 0x2C0 | ADC report | `ADC` | Orange |
| 0x300 | Stats report | `STATS` | Gray |
| 0x555 | Timestamp | `TS` | Light blue |
| 0x600/0x700 | SML smart meter | `SML` | Amber |

**Statistics counters added:** `g_proto_heizung_id_cnt`,
`g_proto_heizung_temp_cnt`, `g_proto_heizung_pwm_cnt`,
`g_proto_heizung_adc_cnt`, `g_proto_heizung_stats_cnt`.

**Stats line format changed from:**
```
Proto: TS:%lu SML:%lu TB:%lu Ukn:%lu
```
**to:**
```
Proto: TS:%lu SML:%lu TB:%lu HID:%lu HTmp:%lu HPWM:%lu HADC:%lu HSt:%lu Ukn:%lu
```

## haus_can.h Interface Summary

```
CAN IDs (11-bit standard):
  0x180 + rank  → Sensor identity (DS18B20 ROM)
  0x200        → Temperature data (4 × int16_t c°)
  0x240        → PWM command
  0x280        → PWM report (state)
  0x2C0        → ADC measurement window
  0x300        → CAN bus health report
  0x555        → System timestamp broadcast
  0x600/0x700  → SML smart-meter data (alternating)

Structures:
  haus_can_identity_t       — 8-byte DS18B20 ROM
  haus_can_temperature_t    — HAUS_CAN_MAX_SENSORS int16_t c° values
  haus_can_pwm_t            — duty (permille), frequency (Hz), flags
  haus_can_adc_t            — max/min/peak-to-peak counts, flags
  haus_can_stats_t          — sent/recv/refused counters, EFLG, CANSTAT
  haus_can_timestamp_t      — seconds, minutes, hours, weekday
  haus_can_sml_message_t    — SML message (length + 256 bytes payload)
  haus_can_sml_frame_t      — per-frame SML (8 bytes + length)

Inline helpers:
  haus_can_set_*()  — pack struct → uint8_t[8]  (identity, temperature, pwm,
                      adc, stats, timestamp, sml_frame)
  haus_can_get_*()  — uint8_t[8] → struct
  haus_can_get_rank() — extract sensor rank from CAN ID
```

Note: `haus_can_sml_message_t` is a convenience container for reassembling a
whole SML message; it has no `set_/get_` pair (only `haus_can_sml_frame_t` and
the `haus_can_sml_get_message_length()` / `haus_can_sml_get_frame_offset()`
helpers operate on the wire frames).

## Backward Compatibility

- **AVR projects:** The `can_send_*()` API in `can_sender.h` is unchanged.
  Existing call sites work without modification (only the include line in
  `main.cpp` was added).
- **ESP32 projects:** `CAN_ID_TIMESTAMP` and
  `CAN_ID_STROMZAEHLER_INFO_BASIS` retain their exact values (0x555 and 0x600).
  The legacy `can_ids.h` files remain on disk and define the same two macros,
  so they are drop-in replacements.

## Rollback Plan

The migration is behavioural, not include-only: `can_sender.cpp` and
`26_CYD_CAN_monitor/src/main.cpp` were rewritten around the new helpers, so
reverting the includes alone will not restore the old code.  To roll back,
revert the source changes together (git) rather than editing includes in place.

The legacy `can_ids.h` files are still present, so the old ESP32 include
declarations continue to resolve if the sources are reverted.

## Future Work

1. `haus_can.h` now lives in `<repo>/include/` and is linked into every ESP32
   project via `make links`; extend it with new protocol definitions there.
2. Consider a `haus_can_rx()` helper for receiving and decoding frames.
3. Add unit tests for the `haus_can_set_*()` / `haus_can_get_*()` round-trip.
4. Remove the now-unreferenced `can_ids.h` files once no rollback is needed.