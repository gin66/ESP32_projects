# Review: `whitepaper.md` — 28_hauszentrale / WAVESHARE-ESP32-S3-Touch-LCD-7

**Reviewed:** `ESP32/28_hauszentrale/docs/whitepaper.md` (rev 1.0, 1661 lines)
**Reviewed against:** `boards/WAVESHARE-ESP32-S3-Touch-LCD-7-7inch/source/`,
`boards/WAVESHARE-ESP32-S3-Touch-LCD-7-7inch/README.md`, `include/haus_can.h`,
`ESP32/Makefile`, `ESP32/prj_template/{include,src}/`, `ESP32/26_CYD_CAN_monitor/`,
`AGENTS.md`

> **Status against rev 1.3.** This review was written against rev 1.0. Revs 1.2
> and 1.3 added the climate sensors, PWM duty control, the declarative-UI section,
> the memory map, the tiered history store, the CAN enable switch, the type scale
> and the nav pane. Several findings closed along the way; everything else stands.
>
> | Finding | Now |
> | ------- | --- |
> | **B4** — `sizeof(Sample) == 12` cannot hold | ✔ Closed. The record is 24 B and the assert, §9.2, §9.5, §9.7, D7/D8 all agree. |
> | **C4** — F5 says three zones, §8.9 delivers four | ✔ Closed. F5 now lists five, matching §8.5. |
> | **C5** — Q7 recommends against TX while §7.2 transmits | ✔ Closed. Q7 is in §12.1 Resolved: yes, for one frame (`0x240`). |
> | **A3** — `LV_LABEL_LONG_CLIP` is the LVGL 8 spelling | ✔ Closed, incidentally — §8.12 now names `LV_LABEL_LONG_MODE_WRAP`. |
> | **A7** — `power_w` is `int16_t` and "scaled" | ✔ Closed. `HausState.power_dw` is `uint16_t` decawatts (§10.2). |
> | **C2** — block CRC vs O(1) seek | ➖ Superseded. Rev 1.3 replaced the append model with a once-a-day whole-file write (§7.10), so per-block framing is moot. |
> | **B1** — CH422G bit numbers wrong (`USB_SEL` is bit 5, not 4) | ✓ **Closed.** Corrected throughout: `USB_SEL`=IO5, `SD_CS`=IO4, `LCD_RST`=IO3, `LCD_BL`=IO2, `TP_RST`=IO1. A **third** source confirms it — Espressif's own pin table in `esp_expander_ch422g.hpp` documents IO0–IO7 and OC0–OC3 as expander pins 0–7 and 8–11. The board README's prose table is off by one against all three *and against its own register example*. D47. |
> | **A4** — "the module must not go through `esp_io_expander`" | ✗ **Overtaken — I was too strong.** `esp_io_expander` is the right foundation: Espressif-maintained, already used by the Waveshare examples, and it supplies the pin map, the all-or-nothing IO0–IO7 direction control, typed `enableOC_PushPull()`/`enableOC_OpenDrain()`, and `multiSetIO()` for batched writes. §6.5 rewritten to build on it, adding only a cached shadow (so a setter is one write, not a read-modify-write), a mutex, and named board-meaningful setters. The two genuine traps stay as documented caveats: `write_output_reg()` skips a zero byte, and `begin()` leaves IO0–IO7 HIGH. D46. |
> | **C1** — CH422G power-on default | ✓ **Closed by source.** Three statements agree the output register comes up **HIGH**: the driver's `REG_WR_IO_DEFAULT_VAL 0xFF`, the wrapper's documented `begin()` ("sets all IO0-7 pins to output high-level mode"), and `USB_SEL`=IO5. **The board powers up in CAN mode with USB dead.** R1 as written in rev 1.0 was wrong; §3.3 now states the correction and makes the guarantee a *firmware* obligation (N19) plus a hardware escape (Q17). D48. Bench-confirm the ROM-download escape still works in phase 1 — it now works for a different reason than rev 1.0 claimed. |
> | **G2** — phase gates do not cover Q9–Q11 | ✓ Closed. Phase 0 now reads "Q1–Q15 answered or deferred with an owner"; rev 1.3 extends this to Q21. |
> | **G3** — unresolved question inside the §6.7 code block | Still open. The `"if the board has one"` comment survives. |
> | **A1/A2** — `tpl_backlightPin 2` is DATA12; `tpl_ledPin_R/G/B` includes DATA3 | ⬆ **Now urgent, and doubled.** Rev 1.3 wants GPIO 4 for the CAN force-USB strap (Q17) *and* for LEDC backlight dimming (§7.13 option D, Q19). Both compete for the one free pin, and §6.7 still claims it as `tpl_ledPin`. Three claims on one GPIO — reconcile before phase 1. |
>
> **New in rev 1.3, not yet reviewed here:** the 24-hour power-loss window from
> once-a-day SD writes (R23), the modelled water-out temperature (R24), the 4 mm
> floor's cost in information density (R25), SML decoder drift from project 15
> (R26), and — the largest — **Q21**: none of this board's driver stack is
> ESP-IDF core. All of it is registry components, and `esp_lcd_rgb_panel` needs
> IDF v5.2+, while PlatformIO's default `framework = arduino` is IDF 4.4.4 where
> *none* of it exists. §6.3 cannot start until that is settled.


## Summary

The design reasoning is strong. Four ideas in this document are better than
anything else in the repo and should be treated as house style:

- **F14 / §7.7** — rank is a derived, shiftable address; the function belongs to
  the sensor. This is the kind of correctness argument most embedded projects skip.
- **Gap markers over interpolation** (D9 / §9.3) — refusing to assert measurements
  that were never taken.
- **Three-sink logging with the ring authoritative** (§7.6) and the *pull*-based
  WebSocket drain with an explicit `{dropped: N}` marker (D16).
- **Showing the dropped-line counter on the log screen** (§8.12) — it turns "the
  log is empty" from a confusion into a diagnosis.

However, there are **6 blocking defects** (B1–B6) that would break the build, the
recovery path, or the on-device data, plus **5 design contradictions** (C1–C5)
where two sections of the document disagree with each other. B1 and C1 are the
expensive ones: the CH422G bit numbers are wrong, and the USB/CAN recovery story
rests on a power-on default that the vendor's own register defaults contradict.

Findings are grouped as:

| Group | Meaning |
| ----- | ------- |
| **B** | Blocking — fix before phase 1 |
| **C** | Contradiction — two sections disagree; needs a decision |
| **A** | Accuracy — factually or arithmetically wrong, non-fatal |
| **G** | Gap — underspecified, will surface during implementation |
| **V** | Verified correct (listed for completeness) |

---

## B — Blocking

### B1. CH422G bit numbers are wrong; `USB_SEL` is bit 5, not IO4/bit 4/bit 3

**Where:** `whitepaper.md:152-156`, `:167`, `:178`, `:202-206`, `:381`, `:384`,
`:387`, `:390-391`, `:395`, `:486`, `:640`, `:644`, `:739`, `:1661`

The document names the expander signals four different ways and none of them
match the vendor sources:

| Signal | Whitepaper says | Vendor headers say |
| ------ | --------------- | ------------------ |
| TP_RST  | IO0 (`whitepaper.md:202`, `:156`) | `1` |
| LCD_BL  | IO1 (`:203`, `:155`) | `2` |
| LCD_RST | IO2 (`:204`, `:154`) | `3` |
| SD_CS   | IO3 (`:205`, `:153`) | `4` |
| USB_SEL | IO4 (`:206`, `:167`, `:390`, `:1661`) and **"bit 3"** (`:486`) | **`5`** |

**Evidence.** All three Waveshare port headers agree with each other:

- `source/waveshare_lcd_port.h:14-18`
- `source/waveshare_twai_port.h:12-16`
- `source/waveshare_sd_card.h:6-10`

```c
#define TP_RST 1
#define LCD_BL 2
#define LCD_RST 3
#define SD_CS 4
#define USB_SEL 5
```

The board README's table (`README.md`, "CH422G IO Expander") has the same
off-by-one as the whitepaper — but the README's own *register* example
contradicts its table and agrees with the headers:

```c
i2c_master_write_to_device(I2C_NUM_0, 0x24, &enable_buf, 1, portMAX_DELAY);
i2c_master_write_to_device(I2C_NUM_0, 0x38, &usb_sel_high, 1, portMAX_DELAY);
usb_sel_high = 0x20   // bit 5 set
```

`0x20` = bit 5. Two independent sources say bit 5; the whitepaper's "bit 3"
(`:486`) does not correspond to either table.

**Why it matters.** It is not cosmetic. Following the document's own reasoning
(`:486`, "bit 3 (USB_SEL) is the only one any project may ever want to change"),
the natural implementation writes `0x08` — which under either numbering lands on
LCD_RST or SD_CS, not the mux. Writing `0x10` (bit 4, the README's table) clears
SD_CS, selecting the SD card, while leaving the device in CAN mode with USB dead
and the card's chip-select asserted — the exact failure the doc spends §3.3 and
R1 trying to prevent.

**Fix.** Replace §3.2, §3.4, §6.2 and §6.5 with a single authoritative bit table
sourced from the port headers, and add the register addresses while you are there
(they are needed anyway — see A4):

| Register | Addr | Purpose |
| -------- | ---- | ------- |
| WR-SET | `0x24` | `IO_OE` (bit 0, all-12 direction), `OD_EN` (bit 2), `SLEEP` (bit 3) |
| WR-OC  | `0x23` | Open-drain / push-pull per pin (OC0–OC3) |
| WR-IO  | `0x38` | Output pattern (the "pattern byte") |
| RD-IO  | `0x26` | Input readback |

Then fix the four in-code references: `:381` (IO1→IO2), `:384` (IO2→IO3),
`:387` (IO3→IO4), `:390-391` (IO4→IO5), `:486` (bit 3 → bit 5).

---

### B2. `tpl_waveshare_lcd7.cpp` is unguarded — this breaks all 27 existing projects

**Where:** §6.7 (`whitepaper.md:507-510`) guards only the *header*.

`ESP32/Makefile:16` symlinks **every** template source into **every** project:

```make
for i in [0-9]*/src; do (cd $$i; ln -sf ../../prj_template/src/*.cpp .); done
```

Adding `tpl_waveshare_lcd7.cpp` to `prj_template/src/` therefore puts it into the
build of projects 01–27 the next time anyone runs `make links`. None of them
define `IS_WAVESHARE_LCD7`, so the translation unit must be empty for them.

**Fix.** Wrap the entire `.cpp` body:

```cpp
#include "tpl_waveshare_lcd7.h"

#ifdef IS_WAVESHARE_LCD7
// ... all implementation ...
#endif
```

State this requirement explicitly in §6.7 next to the header-guard note, and add
it to the phase-1 exit criterion: `cd ESP32/26_CYD_CAN_monitor && pio run -e
esp32ota` must still pass after the file lands.

**Side effect, worth noting:** `Makefile:13` also symlinks every template header
into every project. The `#if defined(IS_WAVESHARE_LCD7)` guard in the header is
therefore load-bearing for 27 projects too — the document gets that right, which
is what makes B2 easy to miss.

---

### B3. `platformio.ini` omits the PSRAM configuration the board requires

**Where:** §7.1 (`whitepaper.md:551-589`)

The board has **8 MB octal** PSRAM (`README.md` specs table: "8 MB Octal SPI";
`whitepaper.md:122` states this correctly). The proposed `platformio.ini` has:

```ini
board = esp32-s3-devkitc-1
```

and no `board_build.arduino.memory_type`. Without `= qio_opi` the Arduino core
assumes `qio_qspi` and PSRAM does not initialise. That silently invalidates:

- N6 — the 768 KB decoded background buffer (§8.3)
- N8 — "fonts and background images live in PSRAM"
- §7.6 — the **1 MB PSRAM log ring**, which is the project's primary diagnostic
  surface
- §9.2 — the claim that a month of records fits in PSRAM

Every one of those is load-bearing. This is the single most consequential
omission in the `platformio.ini` block.

**Also missing, same section:**

- `-DCONFIG_SPIRAM_CACHE_WORKAROUND` — used by `26_CYD_CAN_monitor` and called out
  in `AGENTS.md` as required for PSRAM stability.
- `-DARDUINO_USB_MODE=…` (and `ARDUINO_USB_CDC_ON_BOOT`). Q2 (`:1563`) frames this
  as a bench discovery, but it is a compile-time configuration choice. If the core
  brings up `HWCDC` at boot it claims GPIO 19/20 before TWAI can ever have them —
  see also A6. Decide it in `platformio.ini`, not at the bench.
- `monitor_speed = 115200` and `upload_speed`, both present in
  `26_CYD_CAN_monitor/platformio.ini`. Minor, but the `esp32usb` env is the
  recovery path (R1), so it should be complete.
- `platform_packages =` — present in project 26's `[common]`, absent here. Harmless
  if empty, but it is the repo convention.

**Fix.** Add to `[common]`:

```ini
board_build.arduino.memory_type = qio_opi
-D CONFIG_SPIRAM_CACHE_WORKAROUND
```

and pin the USB mode explicitly with a comment referencing Q2.

---

### B4. `static_assert(sizeof(Sample) == 12)` cannot hold — the error propagates through §9

**Where:** §7.4 (`whitepaper.md:675-683`)

```cpp
struct Sample {
  uint32_t epoch;        // 4
  int16_t  chamber_c100; // 2
  int16_t  water_in_c100;// 2
  int16_t  water_out_c100;// 2
  uint16_t power_w;      // 2
  uint8_t  flags;        // 1
};
static_assert(sizeof(Sample) == 12, ...);
```

`4 + 2 + 2 + 2 + 2 + 1 = 13` bytes; padded to **14** without `__attribute__((packed))`.
The assert fails at compile time, and the 12-byte figure is load-bearing well
beyond this line:

| Location | Stated with 12 B | Correct with 14 B |
| -------- | ---------------- | ----------------- |
| §9.2 (`:1308`) 1 day | 17 KB | 20 KB |
| §9.2 (`:1309`) 1 week | 121 KB | 141 KB |
| §9.2 (`:1310`) 1 month | 518 KB | 605 KB |
| §9.5 (`:1385-1387`) | same three | same three |
| §9.7 retention (`:1428`) | 24 mo ≈ 12 MB | 24 mo ≈ 14.5 MB |
| §9.7 free-space guard (`:1431`) | 50 MB | unaffected, but re-check |

Note `haus_can.h` uses `__attribute__((packed))` throughout — following that
convention would give 13, still not 12.

**Two clean resolutions.** Pick one and re-derive the table above:

1. **Keep 14 bytes (recommended).** Alignment 4 is worth 1 byte per record for
   `memcpy`-free access and no unaligned reads on the SD DMA path. Change the
   assert to `== 14`.
2. **Reach 12.** Fold `flags` into the top byte of `epoch` — but `epoch` needs all
   32 bits until 2038 as minutes-since-epoch, so this does not work. Instead
   store `epoch` as `uint32_t` minutes and drop `power_w` to `uint8_t` (0–2550 W
   covers heater power, not household power — conflicts with Q1). **Option 1.**

Whichever is chosen, the assert's comment ("fixed-size records enable seeking")
is right and should be kept — it is the property, not the number, that matters.

---

### B5. §10.3's flush arithmetic is wrong by an order of magnitude

**Where:** §10.3 (`whitepaper.md:1503`), reused in D3 (`:1629`)

> A full-screen flush at 16 MHz pixel clock is 480 × 800 × 16 bit / 16 MHz ≈ **38 ms**.

The correct value: 384 000 px ÷ 16 MHz = **24 ms**, i.e. a full-frame panel
refresh of ≈41.7 Hz. (480 × 800 and 800 × 480 are the same pixel count, so the
rotation does not change it.)

The stated 38 ms is wrong, and the conclusion drawn from it is worse than the
number:

> At 20 Hz that is 77 % of a core just for the animated regions

At 20 Hz × 38 ms that would be 76 % — so the arithmetic is internally consistent,
but both the per-frame figure and the implicit assumption that LVGL redraws
full-screen are wrong. The cost table immediately above it (`:1496-1501` —
1 %, 3 %, 1 %, 8 %) has no derivation at all and is presented as fact.

This matters because §10.3 is the quantitative basis for **R3** ("RGB flush
bandwidth makes the UI stutter"), and R3's mitigation is "measure in phase 5".
So the risk register is leaning on numbers nobody computed.

**Fix.** Either derive the table or mark it explicitly as an estimate. If you
want a defensible estimate: the only hard number is the panel's scan-out rate
(24 ms full frame, 41.7 Hz). Partial-region invalidation at 20 Hz over ~120 × 140
px ≈ 16 800 px × 20 Hz = 336 000 px/s ≈ 3.4 % of the 9.8 Mpx/s the panel can move
— which is reassuring, and is the argument R3 actually needs. State that, and
keep "measure" as the mitigation rather than as a substitute for the reasoning.

---

### B6. The board reference path does not exist

**Where:** §3.1 (`whitepaper.md:132`), §14.A (`whitepaper.md:1610`)

Both point at `boards/WAVESHARE-ESP32-S3-Touch-LCD-7/`. The actual directory is:

```
boards/WAVESHARE-ESP32-S3-Touch-LCD-7-7inch/
```

Fix both. Since the whitepaper repeatedly leans on this directory as the source
for §3.2/§3.4, and B1 shows that directory's *table* is itself off-by-one against
its own register examples, cite `source/waveshare_lcd_port.h` specifically rather
than the README.

---

## C — Contradictions

### C1. R1's recovery guarantee rests on a power-on default the vendor registers contradict

**Where:** §3.3 R1 (`whitepaper.md:178-180`), §7.5 (`whitepaper.md:739-741`)

> **R1 — Boot must default to USB.** The CH422G powers up with IO4 low, so a
> board that is bricked can always be reflashed by power-cycling.

> A watchdog restart while CAN is enabled brings it back with `IO4` low (the
> CH422G powers up that way): CAN stays alive and USB returns.

The Espressif CH422G driver sets the output register to all-ones on reset
(`source/esp_io_expander_ch422g.c`):

```c
#define REG_WR_IO_DEFAULT_VAL   (0xFFUL)
```

All twelve outputs high — including USB_SEL, i.e. **CAN mode with USB dead**. The
board README makes the same claim ("USB_SEL = LOW (default)"), so the whitepaper
inherited the error rather than inventing it; but the register default says
otherwise, and the safety claim is load-bearing for R1, R2 and §7.5's whole
recovery narrative.

There is a second, independent problem in the vendor example, worth knowing
before phase 1 touches `waveshare_twai_port.c` as a reference
(`source/waveshare_twai_port.c:52-56`):

```c
// When USB_SEL is HIGH, it enables FSUSB42UMX ... and then dont use USB Function
uint8_t write_buf = 0x01;
i2c_master_write_to_device(I2C_NUM_0, 0x24, &write_buf, 1, ...);  // WR-SET  = 0x01 → all outputs
write_buf = 0x20;
i2c_master_write_to_device(I2C_NUM_0, 0x38, &write_buf, 1, ...);  // WR-IO   = 0x20 → bit 5 = USB_SEL HIGH
```

`0x20` is bit 5, so this example is self-consistent — but it is written to
`0x38`, which per `esp_io_expander_ch422g.c:30` is **WR_IO**, not WR_SET. So the
direction-enable write at `0x24` uses `0x01` (IO_OE) and the pattern write at
`0x38` uses `0x20`. Meanwhile the README's USB example writes `0x24` then `0x38`
with a comment calling `0x38` "WR-IO", and the README's SD example calls the
same `0x38` "WR-IO" — but the README's *backlight* example calls `0x38` "WR-IO"
too, while its "output mode" step writes `0x01` to `0x24` and calls that WR-SET.
So the register map is at least consistent; only the bit numbering is contested.

**This is a bench question with a cheap answer.** Measure CH422G IO5 at reset
with a scope or a multimeter before writing a line of `tpl_waveshare_lcd7`.
Whichever way it comes out, the mitigation differs:

- **If it comes up LOW** — R1 holds as written, but the guarantee comes from the
  CH422G's POR behaviour, not from anything the firmware does. Say so, and note
  that a firmware crash *after* `USB_SEL` goes high still needs a power cycle
  (the document already says this at `:741`).
- **If it comes up HIGH** — R1 is inverted and the "bricked board is always
  reflashable" claim is **false**. The mitigation becomes: drive USB_SEL low as
  the *first* thing in `setup()`, before anything that can fail, and accept that
  a board wedged in CAN mode needs BOOT + power-cycle (which the document already
  describes at `:189-190`).

Either way, restate it as a verified fact with the measurement attached, not as a
premise. This is the one finding where being wrong is expensive.

---

### C2. §9.7's block CRC invalidates §9.3's O(1) seek and §7.4's `count()`

**Where:** §9.7 (`whitepaper.md:1437-1439`) vs §9.3 (`:1326`) and §7.4 (`:690`)

§9.3, D7 and the whole of §9.5 rest on a flat record array:

```
seek(index) = index * 12                          O(1)
```

and §7.4's interface offers `virtual size_t count() const`.

§9.7 then proposes:

> Records are appended in blocks of 64 with a trailing CRC32, so a power cut
> during a write is detected at read time.

Block framing means `index * 12` no longer addresses a record, and
`count()` can no longer be `filesize / 12`. Both are load-bearing: §9.5's
read-window table, the LRU cache key `(range, end_index)` (`:1395-1396`), and the
§9.3 streaming downsampler (`:1336-1342`) all index by absolute record number.

**Pick one:**

- **Keep the CRC, pay for it.** Specify: `count()` walks block headers once at
  open and caches the result; `read(first, n)` converts record index → byte offset
  via the cached block table. The block table for a month is 675 entries × 4 B ≈
  2.7 KB — trivial, and it preserves O(1) *after* a one-time open cost. This is
  the better answer and it is not in the document.
- **Drop the CRC.** Power-cut resilience is then handled by the gap markers
  §9.3 already draws: a truncated tail simply produces a short file and the
  columns past `count()` are gaps, which is exactly the honest rendering the rest
  of the document insists on. Simpler, and arguably more consistent.

Either way, reconcile the two sections and add the chosen mechanism to D7.

---

### C3. §10.2 never performs the §7.7 demux — the recorder has nothing correct to write

**Where:** §10.2 (`whitepaper.md:1472-1486`) vs §7.7 (`:896-901`) and §7.4 (`:675-683`)

`HausState` is **slot**-indexed:

```cpp
int16_t temp_c100[HAUS_CAN_MAX_SENSORS];   // HAUS_CAN_TEMP_EMPTY == absent
```

But §7.4's `Sample` is **function**-indexed (`chamber_c100`, `water_in_c100`,
`water_out_c100`), and the whole of F14/§7.7 exists to get from one to the other
via `slot → rank → ROM → function`.

No section states where that demux happens. The obvious reading — "the recorder
resolves it" — means the resolution logic sits in the recorder task, which is
wrong: the resolution belongs to the CAN task, which is the single writer to
`HausState` (§5, `whitepaper.md:324-327`) and the only place with fresh identity
frames. Doing it downstream also breaks N5's "pure C++, no dependencies" boundary
in a subtle way, because the bindings come from SPIFFS config.

**Fix.** Add a function-indexed array to `HausState` and apply the resolution in
the CAN task:

```cpp
enum class SensorFn : uint8_t { Chamber, WaterIn, WaterOut, Board, None, COUNT };

struct HausState {
  int16_t temp_c100[HAUS_CAN_MAX_SENSORS];   // raw, by slot — for the CAN monitor view
  int16_t temp_by_fn[(uint8_t)SensorFn::COUNT];
  uint8_t fn_of_slot[HAUS_CAN_MAX_SENSORS];  // SensorFn, or None
  // ...
};
```

`temp_c100[]` stays because the CAN monitor screen (§8.10) needs the raw
slot-indexed view; `temp_by_fn[]` is what the home screen and the recorder
consume. Then §7.4's `Sample` fields map directly off `temp_by_fn[]` and the
question is answered.

---

### C4. F5 says three zones; §8.9 delivers four

**Where:** F5 (`whitepaper.md:229`) vs §8.9 (`:1157-1160`)

F5: *"Three tappable zones (one per temperature) opening a detail screen each."*
§8.9: *"Reached by tapping the power readout rather than one of the three
temperature zones (F5 lists three zones; the power area is a fourth, documented
affordance)."*

§8.9 is honest about the discrepancy and §11.D's Screen enum (`:1194`) has all
seven screens. But a requirements table that understates the deliverable is a
requirements table that will fail an acceptance review. The §8.5 zone table
(`:1080-1085`) also omits the power zone.

**Fix.** F5 → four zones (three temperature + power). Add the power row to §8.5's
table with its region. F6/F7/F8 stay as they are.

---

### C5. Q7 recommends against TX while §7.2 already commits to it

**Where:** §7.2 (`whitepaper.md:629-633`) vs Q7 (`:1567`) and §2.2 (`:104`)

§7.2 specifies, without hedging:

> **TX:** one `0x555` frame per second carrying wall clock, plus a heartbeat on
> the device's own node ID

Q7 asks whether the panel should write to the bus at all and records
*"Recommend no: this is a listener."* §2.2's Out of Scope says *"CAN transmit of
heater commands. This device listens."* — narrower, and compatible with §7.2, but
Q7's blanket recommendation is not.

This is a decision already made in the body of the document and re-opened in
§12, which is the inversion §0 says will not happen.

**Separately, worth checking before committing:** `haus_can.h:64` says `0x555` is
*"Sent by every ESP32 that has CAN. One frame per second."* Project 26 already
does this. Adding a *second* per-second frame from a new node doubles that
node's contribution to a shared 125 kbps bus for no consumer benefit that Q7
itself identifies. Recommend: keep the `0x555` broadcast (it is the documented
contract and the `TIME-BCAST` consumer understands it), drop the separate
heartbeat, and let the node ID be inferred from the existing `0x300` stats frame
if it is needed at all.

**Fix.** Resolve Q7 in favour of whatever §7.2 decides, and move the bus-load
note into §7.2 as a real consideration.

---

## A — Accuracy

### A1. `tpl_backlightPin 2` is DATA12 of the RGB bus

**Where:** §6.7 (`whitepaper.md:517`)

```cpp
#define tpl_backlightPin 2   // CH422G IO1 — logical, not a real GPIO
```

GPIO 2 is `EXAMPLE_LCD_RGB_IO_DATA12` (`source/waveshare_lcd_port.h:60`). The
document's own warning at `:521-524` — *"the next reader will try to
`analogWrite()` it"* — is correct, and then the fix it proposes creates the
hazard it just warned about.

The justification is also confused: `tpl_wifi_setup()`'s third argument is the
**LED** pin, not the backlight (`template.h:29`,
`tpl_wifi_setup(true, true, (gpio_num_t)tpl_ledPin)`). `tpl_backlightPin` plays no
part in keeping that call compiling, so `:521-522`'s reasoning does not hold.

**Fix.** Do not define `tpl_backlightPin` for this board at all. Backlight goes
through `tpl_ws7_backlight()` (§6.2). An undefined macro is a compile error; a
`#define`d GPIO 2 is silent pixel corruption.

### A2. `tpl_ledPin_R/G/B` must stay undefined — GPIO 17 is DATA3

**Where:** §6.7 (`whitepaper.md:514-518`)

`tpl_system.h:16-20` and `:25-29` define `tpl_ledPin_R/G/B` as 4/16/17 on both
CYD boards. §6.7's block correctly defines only `tpl_ledPin` for the Waveshare
board, but says nothing about the RGB triplet — and **GPIO 17 is DATA3**
(`source/waveshare_lcd_port.h:47`).

Any template or project code that reaches for `tpl_ledPin_B` on this board
corrupts the display. Add an explicit warning:

```cpp
// tpl_ledPin_R/G/B are deliberately NOT defined: GPIO 17 is RGB DATA3.
// tpl_ledPin (4) is safe — GPIO 4 is unused on this board.
```

Also, `tpl_ledPin 4` is annotated `// BOOT-adjacent status LED, if the board has
one` — see G3; there is no such LED.

### A3. `LV_LABEL_LONG_CLIP` is the LVGL 8 name

**Where:** §8.12 (`whitepaper.md:1240`)

In LVGL 9 the enum is `lv_label_long_mode_t`, with member
`LV_LABEL_LONG_MODE_CLIP`. `LV_LABEL_LONG_CLIP` no longer exists. The reasoning
for using `lv_label` over `lv_list` is sound; only the constant is stale.

### A4. §3.4's description of the CH422G register model is too vague to build from

**Where:** §3.4 (`whitepaper.md:213-217`)

> The Waveshare examples write a mode byte to one register and a pattern byte to
> another.

Correct but not actionable, and it misses the one property that actually bites.
From `source/esp_io_expander_ch422g.c:151-176`:

```c
static esp_err_t write_output_reg(esp_io_expander_handle_t handle, uint32_t value)
{
    uint8_t wr_oc_data = (value & 0xF00) >> 8;
    uint8_t wr_io_data = value & 0xFF;

    if (wr_oc_data) { /* write WR-OC */ }   // <-- zero is silently skipped
    if (wr_io_data) { /* write WR-IO */ }   // <-- zero is silently skipped
    return ESP_OK;
}
```

**A computed byte of `0x00` is a no-op.** You cannot clear all bits through the
Espressif API. Any "all peripherals off" pattern would be silently ignored.

§6.5's shadow-pattern design is the right shape and avoids this — but it needs to
say explicitly that the module must do raw `i2c_master_write_to_device()` calls to
`0x24` (WR-SET) and `0x38` (WR-IO), and must **not** go through
`esp_io_expander`. Otherwise the first person to reach for the vendor API
reintroduces the bug.

Also worth stating in §6.5: SD_CS must be **push-pull**, not open-drain, to
present a hard low. `esp_io_expander_ch422g_set_oc_open_drain()` /
`set_oc_push_pull()` toggle bit 2 (`OD_EN`) of WR_SET; the document mentions
output-enable and mode registers but never names the open-drain choice.

### A5. Chart width: §9 uses 480 columns, §8.6/§9.4 specify a 440 px chart

**Where:** §9.2 (`whitepaper.md:1304`), §9.3 (`:1330`), §9.6 (`:1417`) vs §8.6
(`:1121`) and §9.4 (`:1359`)

§8.6 and §9.4 both place the chart at **440 × 260 at x=20**. §9.2 says "At 480 px
of chart width", §9.3 declares `480 columns`, and §9.6's precision claim ("~3
minutes at 1 day", "~90 minutes at 1 month") is derived from 480.

1440 / 480 = 3.0 and 43200 / 480 = 90 — so the arithmetic is internally
consistent with 480 and wrong with respect to the stated chart geometry. At 440
columns the figures are 3.27 and 98.2 minutes.

**Fix.** Use 440 consistently, or make the chart full-width. If the 20 px side
margins are cosmetic and the chart can be 480 wide, say so and drop the margins.

### A6. `tpl_ws7_gpio19_20_mode()` promises an IOMUX that does not exist

**Where:** §6.2 (`whitepaper.md:392`, `:395-399`), §7.2 (`:639`)

```cpp
esp_err_t tpl_ws7_gpio19_20_mode(bool as_usb);  // USB-SERIAL-JTAG vs IOMUX GPIO
```

> once `IO4` goes high, pins 19/20 must be **IOMUX GPIOs for the CAN peripheral**,
> not USB.

On the ESP32-S3, GPIO 19/20 *are* the native USB D−/D+ pins, which is correct
(`whitepaper.md:163-164`). But there is no IOMUX register that routes them between
"USB" and "GPIO" — USB-SERIAL-JTAG and USB-OTG are separate peripherals that claim
those pins *only while they are initialised*. The conflict is real; the mechanism
described is not.

The actual fix is a build-time decision plus a "who initialised USB first"
ordering rule, which is why it belongs in `platformio.ini` (see B3), not in a
runtime function.

**Fix.** Replace with either:

- A compile-time pin (`-DARDUINO_USB_MODE=0` + a comment that HWCDC must never be
  initialised on this board), or
- A renamed function reflecting what it really does:
  `tpl_ws7_usb_device_release()` — detaches any USB peripheral, idempotent.

Then correct `:395-399` to describe peripheral initialisation rather than IOMUX
switching.

### A7. `power_w` is `int16_t` and "scaled"

**Where:** §7.4 (`whitepaper.md:680`), §10.2 (`whitepaper.md:1476`)

```cpp
uint16_t power_w;      // scaled: see 7.4 power source   (§7.4)
int16_t  power_w;                                     (§10.2)
```

Three problems in two lines: the types disagree between the two structs; "scaled"
is not a unit, so the scaling factor is unspecified; and household power from SML
(Q1) can exceed 32 767 in whatever unit the scale implies.

**Fix.** Name the unit and the scale explicitly — `uint16_t power_dw` (decawatts,
0–6553 kW) or `uint32_t power_mw` — and use one type in both structs. If Q1
resolves to SML, note that the SML decoder must apply the OBIS scale factor before
the value reaches this field.

### A8. §6.2's API is missing an SD-bus accessor and cannot attach the indev

**Where:** §6.2 (`whitepaper.md:367-393`)

- The recorder (§7.4, option C in §6.7) needs the SD SPI bus — MOSI 11 / SCLK 12 /
  MISO 13 — and `tpl_ws7_sd_select()` alone does not provide it. Either expose
  `tpl_ws7_sd_bus()` (an `SPIClass*`) or state that the recorder hardcodes
  `HSPI`/`VSPI` and which port the module initialises.
- `tpl_ws7_touch()` returns an `lv_indev_t` "created with read_cb installed", but
  `lv_indev_create()` takes an `lv_display_t*`. Either `tpl_ws7_touch()` takes the
  display as a parameter, or the module stores it from `tpl_ws7_display()` and the
  doc must say so — plus state that `lv_display_set_default()` is called (the
  `AGENTS.md` lesson from the CYD work).
- `rotation` is accepted by both `setup()` and `display()` (`:369-371`, `:401-403`)
  and the document acknowledges the duplicate-pass hazard but offers no mitigation
  beyond "compile-time-visible mismatch". A `static_assert` on the two values, or
  a single entry point, would actually deliver that.

### A9. `lvgl@^9.2.0` and `ArduinoJson@^7` diverge from repo convention

**Where:** §7.1 (`whitepaper.md:567`, `:569`)

| Package | This document | `26_CYD_CAN_monitor` |
| ------- | ------------- | -------------------- |
| `lvgl/lvgl` | `^9.2.0` | `~9.2.0` |
| `bblanchon/ArduinoJson` | `^7` | `^6.17.2` |

- `^9.2.0` admits 9.3+, and the document's rotation behaviour (N1, D2, R4) depends
  on 9.2-specific semantics. `~9.2.0` is the right range for that dependency.
- `ArduinoJson` v7 is a major bump, and `tpl_websocket.h:19` includes
  `ArduinoJson.h` from the shared template. Mixing majors across projects that
  share template headers is a footgun. If v7 is genuinely needed, that is a
  repo-wide decision and belongs in a note, not a silent divergence.

### A10. `power_w` in `Sample` vs `HausState` field ordering

Minor, but the two structs in §7.4 and §10.2 should be declared once. See C3 — the
resolution of that item naturally unifies them.

---

## G — Gaps

### G1. §13's sequencing notes contradict the phase table

**Where:** §13 (`whitepaper.md:1593-1601`)

> **Sequencing note: phase 5 before phase 7 is deliberate. Building the host
> harness against a stub decoder first means the entire GUI … is developed without
> flashing.

But phase 5 (`:1584`) is the log ring; the host harness and its stubs are phase 6
(`:1585`). The note's reasoning belongs to phase 6, not 5.

> **Second sequencing note: phase 5 before phase 4's tail end.**

Phase 4 precedes phase 5 in the table, and "phase 4's tail end" is not a defined
boundary. The intent — build the log ring before fighting the RGB panel — is a good
one, but as written the note is unactionable.

**Fix.** Renumber or restate: "phase 6 before phase 7" for the harness rationale,
and either drop the second note or fold it into phase 5's exit criterion.

### G2. Phase gates do not cover Q9, Q10, Q11

**Where:** §13 (`whitepaper.md:1579`), §11 R16 (`:1544`)

Phase 0's exit criterion is *"Reviewed; Q1-Q8 answered."* But §12.2 carries **Q9**,
**Q10** and **Q11**, and R16 states *"Q9 must be resolved before phase 6"* — a gate
that appears nowhere in §13. Q11 (opt-in vs default PSRAM ring) has no gate at
all, despite §7.6 changing a shared template module on the strength of it.

**Fix.** Extend phase 0's criterion to "Q1–Q11 answered", or add the specific gates
to phases 5, 6 and 8 where they actually bite.

### G3. §6.7 leaves an unresolved question inside a code block

**Where:** §6.7 (`whitepaper.md:516`)

```cpp
#define tpl_ledPin      4    // BOOT-adjacent status LED, if the board has one
```

There is no status LED on this board (`README.md` "Onboard Peripherals" lists the
BOOT button, RESET, battery connector, USB-C, PH2.0 breakouts and CAN
termination — no LED). More to the point, this contradicts §0's stated policy:

> Open questions are collected in §12 rather than being silently decided.

**Fix.** Drop the tentative comment. GPIO 4 is genuinely free on this board, so
`tpl_ledPin 4` is a safe placeholder for call-site compatibility — say that, and
note that no LED is fitted.

### G4. §8.6's cross-reference points at the wrong section

**Where:** §8.6 (`whitepaper.md:1116`)

> Shared skeleton for all four detail screens (§8.11 defines it)

§8.11 is Navigation and the back button. The shared skeleton is defined in §8.6
itself and referenced by §8.7/§8.8/§8.9. Fix the pointer.

### G5. §8.2's gear position straddles the footer boundary

**Where:** §8.2 (`whitepaper.md:999`, `:1084`)

The ASCII layout puts the footer at y = 740–800. §8.5 places the gear at
`(416, 736)` sized 64 × 64, i.e. spanning y = 736–800 — it starts 4 px above the
band it is described as living in. Cosmetic in a diagram, but if the layout is
implemented from these numbers the gear will overlap the footer edge. Reconcile
the two.

### G6. §4.3's CI screenshot diff needs a stated tolerance policy

**Where:** §4.3 (`whitepaper.md:286-289`)

> CI (or a pre-commit hook) runs a headless smoke test that boots the app, drives
> it through each screen via synthetic taps, and fails on a screenshot diff or an
> assertion failure.

Screenshot diffing across font-rasteriser and LVGL patch versions is the classic
source of a CI that is red for reasons unrelated to the code. If this is going in,
the document should say what the baseline is (pinned LVGL version + pinned host
toolchain) and what the failure policy is (fail, or warn). Also note `make run`
(`:287`) is a new target — the repo's `ESP32/Makefile` currently has `fmt`,
`links`, `build` (`Makefile:1`, `:8`, `:33`) — so that Makefile needs a change the
document does not mention.

### G7. §8.3 decides what Q4 leaves open

**Where:** §8.3 (`whitepaper.md:1028`) vs Q4 (`:1564`)

§8.3 states the background PNG lives in SPIFFS as a settled decision; Q4 asks
whether it should be SPIFFS or SD and records a recommendation for SPIFFS. §0's
policy is that open questions are not silently decided in the body. Either promote
SPIFFS to §12.1 Resolved with Q4's rationale, or soften §8.3 to note the
dependency.

### G8. §9.2's "Samples / pixel column" column header does not match its values

**Where:** §9.2 (`whitepaper.md:1306-1310`)

The column is headed "Samples / pixel column" and reads 3, 21, 90. Those are
samples per column, i.e. the reciprocal of what the header implies. Rename to
"Samples / column" or invert the values. (Arithmetically the numbers are right;
only the label is wrong — and only at 480 columns, per A5.)

### G9. §9.3's `ChartColumn` is ~28 B, not "~18 B"

**Where:** §9.3 (`whitepaper.md:1328-1330`)

```cpp
struct ChartColumn { uint16_t t; int16_t mn[4]; int16_t mx[4];
                     uint16_t mean[4]; uint8_t flags; };
// 480 columns x ~18 B = ~8.6 KB
```

`2 + 8 + 8 + 8 + 1 = 27`, padded to **28**. At 440 columns that is 12.3 KB, and
peak RAM becomes ~18 KB rather than the stated ~15 KB (`:1344`). Still well inside
N6, so this is a numbers-hygiene item rather than a budget problem.

Note also that only **two** series are ever drawn (water-out overlays water-in,
§8.8) — `[4]` is over-provisioned. Dropping to `[2]` gives 16 B, which *is* ~18 B
with padding. That is probably what was intended.

### G10. Draw-buffer strip width is ambiguous under rotation

**Where:** §6.3 (`whitepaper.md:420-424`)

> LVGL's `LV_DISPLAY_RENDER_MODE_DIRECT` with a pair of `hres × N`-pixel strips

With `lv_display_set_rotation(90)` and `LV_DRAW_TRANSFORM_USE_MATRIX`, the
logical resolution becomes 480 × 800 while the panel stays 800 × 480 — so "hres"
is ambiguous between the two. The strip width the DMA actually needs is the
**panel** width (800), not the logical width (480), which is what makes the
documented "~32 KB per buffer" correct (800 × 20 × 2 B).

Say "panel-width (800 px) strips" explicitly. Worth verifying empirically in
phase 2, since it is a 33 % sizing error if read the other way.

### G11. §2.1's Out-of-Scope wording is looser than §7.2

**Where:** §2.2 (`whitepaper.md:104`) vs §7.2 (`:629-633`)

> **CAN transmit of heater commands.** This device listens. It is not a controller.

This is compatible with §7.2's `0x555` broadcast, so it is not wrong — but paired
with Q7's blanket "recommend no" it reads as excluding all TX, which is what makes
C5 confusing. Tighten the wording to "heater *commands*; the panel does
broadcast `0x555`".

---

## V — Verified correct

Checked against source; no action needed. Recorded so a future reader does not
re-litigate them.

| Claim | Location | Verified against |
| ----- | -------- | ---------------- |
| RGB pin table (HSYNC 46, VSYNC 3, DE 5, PCLK 7, DATA0–15 = 14,38,18,17,10,39,0,45,48,47,21,1,2,42,41,40) | §3.2 | `source/waveshare_lcd_port.h:38-63` — exact match |
| I²C SDA 8 / SCL 9, shared with CH422G | §3.2, §3.4 | `waveshare_lcd_port.h:12-13`; CH422G addr `0x24` |
| SD MOSI 11 / SCLK 12 / MISO 13 | §3.2 | `waveshare_sd_card.h:20-22` |
| CH422G I²C address `0x24` | §3.1 | `esp_io_expander_ch422g.h:46` |
| GT911 at `0x5D`, no reset or INT on an ESP32 GPIO | §6.4 | consistent with README I²C map and the port headers |
| Pixel clock 16 MHz; H 4/8/8, V 4/8/8 | §6.3 | `waveshare_lcd_port.h:32-38` — exact |
| `RGB_BOUNCE_LINES = 10` | §6.3 | `EXAMPLE_LCD_RGB_BOUNCE_BUFFER_SIZE (800 * 10)` |
| `LV_COLOR_DEPTH = 16` (RGB565) | §6.3 | `EXAMPLE_LCD_RGB_COLOR_BITS (16)`; 24-bit is the internal path |
| 8 MB flash, 8 MB octal PSRAM | §3.1 | README specs table |
| `USB_SEL = HIGH` ⇒ CAN live, USB dead (the *semantics*) | §3.3 | README + `waveshare_twai_port.c:52` comment. Only the *bit number* is wrong — see B1 |
| GPIO 19/20 are the native S3 USB D−/D+ pair | §3.3 | correct for ESP32-S3 |
| `tpl_sd` is read-only with a hardwired `TPL_SD_CS` default 5 | §6.7 | `tpl_sd.cpp:7-8`, `:39-41` |
| `tpl_debug_log` storage is `static char[100][160]`, internal RAM, 16 KB | §7.6 | `tpl_debug_log.h:10-14`, `tpl_debug_log.cpp:8` — exact |
| `tpl_debug_log` is opt-in via `USE_TPL_DEBUG_LOG` | §7.6 | `tpl_debug_log.h:6` |
| `TaskWebSocketCore0` exists, core 0, 4096 stack | §7.6, §10.1 | `tpl_websocket.cpp:94`, `:203` |
| `min_spiffs.csv` leaves 192 KB SPIFFS (0x30000) | §7.1 | `26_CYD_CAN_monitor/min_spiffs.csv` |
| Proposed partition sizes fit 8 MB (20 KB nvs, 8 KB otadata, 2 × 3 MB app, 512 KB SPIFFS) | §7.1 | arithmetic checks out; nvs/otadata match the existing table's `0x5000`/`0x2000` |
| `-Werror` is the project-26 convention | §7.1 | `26_CYD_CAN_monitor/platformio.ini` |
| `lv_display_set_rotation()` requires LVGL 9.2 | N1, D2 | matches the comment in `26_CYD_CAN_monitor/platformio.ini` |
| `lv_conf.h` is currently borrowed from `19_CYD-project` via symlink | §7.1 | `26_CYD_CAN_monitor/include/lv_conf.h` is a symlink |
| `protocol_detail.cpp` reads `g_can_id_table` / `g_max_can_ids` / `g_sml_count`, owned by `main.cpp` | §7.3 | `protocol_detail.cpp:39-40,142,162,167,344,380-382`; `main.cpp:110-117` — exact |
| `ROW_HEIGHT 18`, `MAX_CAN_IDS` derived from `LOGICAL_HEIGHT` | §7.3 step 5, §8.10 | `26_CYD_CAN_monitor/src/ui/screens.h:11,22` |
| Two divergent `haus_protocol_t` switches (`decode_protocol`, `get_protocol_info`) | §7.3 step 2 | `main.cpp:321`, `protocol_detail.cpp:206` |
| SML reassembly runs in the display path, over the table | §7.3 step 3 | `protocol_detail.cpp:344` |
| `make links` picks up new template files automatically | §6.7 | `ESP32/Makefile:12-13,16` — uses `include/*` and `src/*.cpp` wildcards. **True, and the reason B2 is easy to miss** |
| `HAUS_CAN_TEMP_EMPTY == INT16_MIN` | §8.4, R5 | `haus_can.h:198` |
| `PWM_FLAG_ACTIVE` = `0 < duty < 1000` permille | §8.4 | `haus_can.h:230` |
| Identity frames at `0x180 + rank`, `~1/min + burst at boot` ⇒ 90 s expiry ≈ 3 missed | §7.7 | `haus_can.h:158-165`; `:18` lists 0x180–0x183 |
| `HAUS_CAN_MAX_SENSORS == 4`, so Q9 option (a) needs no protocol change | Q9 | `haus_can.h:60` |
| Rank → ROM table (rank 0/1/2 → 0x180/0x181/0x182) | §7.7 | `haus_can.h:17-19,165,469` |
| `0x555` is "sent by every ESP32 that has CAN, one frame per second" | §7.2 | `haus_can.h:64` |
| SML frames at `0x600` / `0x700` | §7.4 | `haus_can.h:110-111` |
| `haus_can.h` reassembles SML but has no semantic decoder | §7.4 Q1 | correct — only length/offset helpers exist |
| 480 × 800 RGB565 background = 768 KB | §8.3, N6 | 480 × 800 × 2 = 768 000 B |
| Log screen ~44 lines at 16 px over 700 px | §8.12 | 700 / 16 = 43.75 |
| CAN monitor row count ≈ 16 on 480-wide portrait vs 23 on project 26 | §8.10 | 800 / 44 ≈ 18 less header/footer; project 26: 480 / 18 = 26.7 less header ≈ 23 |

---

## Recommended order of work

1. **B1, B6** — cheap text fixes, but everything in §3 and §6 builds on them.
2. **C1** — bench measurement, and it gates the shape of §3.3, R1 and §7.5.
3. **B2** — one `#ifdef`; protects 27 projects.
4. **B3** — two lines of `platformio.ini`; without them PSRAM-dependent
   features (background, log ring, N6, N8, §9.2) silently do not exist.
5. **B4** — decide 12 vs 14 bytes, then re-derive the §9 tables in one pass.
6. **C3, C2** — structural decisions on `HausState` and the record framing; both
   are cheaper now than after phases 6 and 8.
7. **C4, C5, G1, G2** — document consistency; one editing pass.
8. **A-series, G-series** — accuracy and hygiene.

Findings B1, C1 and C3 are the ones worth a second opinion before code is
written; the rest are mechanical.
