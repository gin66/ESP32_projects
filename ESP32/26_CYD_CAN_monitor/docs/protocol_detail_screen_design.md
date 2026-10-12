# CAN Monitor — Protocol Detail Screen Design
## 26_CYD_CAN_monitor

**Date:** 2026-10-12
**Status:** Concept / Design Spec (verified against `main.cpp` @ v1.2)

> **Reviewer's note (2026-10-12):** This document was checked line-by-line
> against the current source. Several design claims and code snippets were
> wrong; the corrections are folded in below and flagged with **✅ corrected**.

---

## Q1 — Raw Mode: Green vs White Lines

**Answer: It is NOT time-related. It is a change-detection flag.**

In `update_can_display()` (`main.cpp:590`):

```cpp
if (entry->used && entry->data_changed) {
    color = lv_color_hex(0x7CFF7C);  // light green — data CHANGED
} else {
    color = lv_color_hex(0xE0E0E0);  // white/grey — unchanged
}
```

`data_changed` is set in `can_table_update()` (`main.cpp:262`) when the new
frame's `data_length_code` or `data[]` bytes differ from the previously stored
`last_data[]` for that CAN-ID:

```cpp
entry->data_changed =
    (entry->data_len != msg->data_length_code ||
     memcmp(entry->data, msg->data, msg->data_length_code) != 0);
```

**What you see:**
- **Light green (`0x7CFF7C`)** — this CAN-ID's payload changed since the
  previous frame received for that ID. E.g. a temperature reading went from
  21.5 °C to 21.6 °C.
- **White/grey (`0xE0E0E0`)** — this CAN-ID is active but its payload has not
  changed since the last frame. E.g. an "alive" frame that repeats the same
  bytes.

**✅ corrected / precise:**
- The flag is recomputed on **every received frame**, not once per 500 ms
  display cycle. The colour you see is just the state at the last
  `update_can_display()` tick (`loop()`, `main.cpp:1312`).
- The green/white `color` variable is only **used in raw mode**
  (`main.cpp:720-722`). In protocol/analyzer mode, every row is recoloured by
  protocol (see the colour table at the end), so green/white never applies
  there.
- It is purely data-content-based, not time-based.
- The flag is a single `bool`: there is **no per-ID frame counter** today, so
  "changed 89 times" cannot be shown without adding a field to
  `can_id_entry_t` (see *Data Gaps* below).

---

## Q2 — Protocol Mode Redesign: Concept

### Problem Statement

The current protocol mode reuses the **same 3-column layout** as raw mode. The
column *headers* still read `ID | Data | Last Seen`, but the *content* is
remapped (`main.cpp:610-634`):

| Column object | Position / width (`screens.c`) | Raw mode shows | Analyzer mode shows |
|---|---|---|---|
| `lblId[i]` | x=5, w=40 | CAN ID hex | CAN ID hex |
| `lblData[i]` | x=47, w=155 | Raw bytes `XX XX …` | **Type label** (`TEMP`, `SML`, `TS`…) |
| `lblTime[i]` | x=204, w=112 | Time / interval | **Decoded value** (`21.50 22.30 …`) |

**✅ corrected:** The original draft claimed the *Data* column (155 px) was too
narrow for decoded content and that *Last Seen* was unused. In fact the decoded
content is written into the **112 px `lblTime` column** — the narrowest one —
and the *Data* column holds the short type label. So the layout is not just
"wrong widths"; the wide column holds the short text and the narrow column
holds the long text.

Other issues remain valid:

1. **Multi-frame messages are fragmented** — SML smart-meter data spans ~30-45
   CAN frames, each with its own CAN-ID (`0x600`, `0x601`, …). Each appears as
   a separate row with partial text like `SML 16/258 B`. No single row tells
   the full story.
2. **No drill-down** — you cannot see the full decoded content of any protocol.
3. **No reliable navigation** — a short tap in the header band toggles
   raw/analyzer (`main.cpp:854-863`), a 1.5 s long-press toggles colour
   inversion (`main.cpp:831-836`). There is no obvious affordance and no way to
   view per-message detail.
4. **Column widths are set in `screens.c`** — the object positions/sizes live
   there (`screens.c:100-145`). Since EEZ Studio is no longer used, edit
   `screens.c` directly (see *Screen 1* below). No regeneration concern.

### Design Goals

- **Tap a protocol row → open a detail screen** (no swiping).
- **Detail screen shows the full decoded message** for that CAN-ID.
- **Multi-frame protocols (SML) are reassembled** and shown as one logical entry.
- **Back button returns to the main list**, preserving scroll/highlight.
- **Works on both 2.8" (320×240 logical) and 3.5" (320×480)** — note both are
  320 px wide, only the logical height differs (`screens.c:96-98`).

---

### Codebase Findings (must-read before implementing)

These are real defects/gaps in the current code that the detail screen work
will either depend on or should fix:

1. **SML "OK" branch is dead code** (`main.cpp:451-467`):
   ```cpp
   if (g_sml_received_length >= expected) {
     g_sml_active = false;
     g_sml_received_length = 0;          // <-- zeroed here
     if (g_sml_received_length > 16) {   // <-- always false!
       ... "SML %d B OK"                 // never reached
     } else {
       ... "SML %d B INCOMPLETE"         // always taken
     }
   ```
   The header sanity check reads `g_sml_received_length` after it has been
   reset. Save the length (or check the actual buffer/header bytes) **before**
   clearing it.

2. **Reassembly uses shared globals, not per-message state**
   (`g_sml_base_id`, `g_sml_received_length`, `g_sml_buffer[258]`,
   `g_sml_active` at `main.cpp:144-147`). `decode_protocol()` runs once per
   used row per 500 ms cycle, appending into the *same* buffer in table order,
   which is not guaranteed to be offset order. Reassembly therefore depends on
   table ordering and re-runs every cycle. Per-message state (keyed by the
   `0x600`/`0x700` base) is the correct fix.

3. **No source-node tracking.** `can_id_entry_t` stores the CAN identifier,
   which for Heizung frames *is* the protocol ID (`0x200`, `0x280`, …). The
   sending node is only identifiable for `TIME-BCAST` frames, where the
   identifier equals the node ID (`main.cpp:1131`). The detail screen's
   "Source: Node 0x1A (192.168.1.26)" line cannot be filled in for most
   protocols without extending the TWAI/RX path — including the IP requires a
   node-ID → IP lookup that does not exist today.

4. **No per-ID counters or history.** `can_id_entry_t` has only a single
   `data_changed` flag, one `last_data[8]`, and `interval_ms`. "Total: 142
   frames | Changed: 89" and a message-history list require new fields and
   bounded storage.

5. **`objects` has no handle to the row background** (`screens.c:110` `bg` is
   local). Programmatic row highlight/click decoration must go through the
   three label objects or use hit-testing.

---

### Screen 1: Main List (Modified)

The main list stays in protocol mode but uses a **type + decoded** column split
and tap-able rows.

```
┌──────────────────────────────────────┐
│ IP: 192.168.1.26 | Node: 0x1A       │
│ Proto: TS:42 SML:7 TB:0 ...         │  ← stats line (unchanged)
│ [Clear]          [toggle band]       │
│                                      │
│ ID     Type     Decoded              │  ← NEW column layout
│ ─────────────────────────────────── │
│ 0200   TEMP     21.50 22.30 19.75    │  ← TAP THIS ROW
│ 0280   PWM      duty=750‰ f=5kHz     │  ← TAP THIS ROW
│ 0555   TS       14:32:18 (day 2)     │  ← TAP THIS ROW
│ 0600   SML      258 B OK             │  ← TAP THIS ROW
│ ...                                  │
└──────────────────────────────────────┘
```

**✅ corrected column plan (both boards are 320 px wide):**

| Column | x | Width | Content |
|--------|---|-------|---------|
| ID | 5 | 48 px | CAN ID hex (e.g. `0200`, or `08ABCDEF` if extended) |
| Type | 55 | 64 px | Protocol label (`TEMP`, `SML`, `STATS`, `TIME-BCAST`, `ID 0x180+0`) |
| Decoded | 121 | 190 px | Full decoded content |

The original draft's `Type = 40 px` is too narrow for `STATS` and
`TIME-BCAST`; 64 px fits both at `montserrat_14`.

**Layout is mode-dependent.** In raw mode the data column needs the full
~155 px for the `XX XX XX XX XX XX XX XX` hex dump, so the widths above are for
analyzer mode only. Since EEZ Studio is no longer used, the simplest approach
is to edit `screens.c` to create the rows with the **analyzer** widths and
simply reposition `lblData[i]` / `lblTime[i]` when switching to raw mode. A
small helper makes this a single call from the mode-toggle path:

```cpp
// In main.cpp, alongside ui_apply_legibility().
static void ui_apply_columns(bool analyzer) {
  int type_w   = analyzer ? 64  : 155;
  int decoded_x = analyzer ? 121 : 204;
  int decoded_w = analyzer ? 190 : 112;

  lv_obj_set_size(objects.lblHeaderData, type_w, 16);
  lv_obj_set_pos(objects.lblHeaderTime, decoded_x, 40);
  lv_obj_set_size(objects.lblHeaderTime, decoded_w, 16);
  lv_label_set_text(objects.lblHeaderData, analyzer ? "Type" : "Data");
  lv_label_set_text(objects.lblHeaderTime, analyzer ? "Decoded" : "Last Seen");

  for (int i = 0; i < MAX_CAN_IDS; i++) {
    lv_obj_set_size(objects.lblData[i], type_w, 16);
    lv_obj_set_pos(objects.lblTime[i], decoded_x, 58 + i * ROW_HEIGHT + 1);
    lv_obj_set_size(objects.lblTime[i], decoded_w, 16);
  }
}
```

This avoids duplicating two sets of label objects in `screens.c`. Call
`ui_apply_columns()` once after `ui_init()` with **the mode the firmware boots
into** (the default is RAW → `false`, headers `Data` / `Last Seen`; hard-coding
`true` leaves RAW rows under `Type` / `Decoded` headers). Re-apply it from the
raw/analyzer toggle, with the new mode, whenever the view flips.

**Row interaction — prefer LVGL events over pixel hit-testing. ✅ corrected.**
The original draft proposed manual bounding-box checks in
`touchscreen_read_cb()`. That is fragile (it runs on every *pressed* poll, so a
single tap can open the detail screen several times, and it duplicates the
existing header-band logic). The row labels are real LVGL objects; make them
clickable once after `ui_init()` and let LVGL manage press/release/click:

```cpp
static void row_click_cb(lv_event_t* e) {
  lv_obj_t* lbl = (lv_obj_t*)lv_event_get_target(e);
  uint32_t idx = (uint32_t)(uintptr_t)lv_obj_get_user_data(lbl);
  if (idx < MAX_CAN_IDS && g_can_id_table[idx].used) {
    open_protocol_detail(idx);
  }
}

static void ui_install_row_clicks(void) {
  for (int i = 0; i < MAX_CAN_IDS; i++) {
    lv_obj_t* row[] = {objects.lblId[i], objects.lblData[i], objects.lblTime[i]};
    for (int c = 0; c < 3; c++) {
      lv_obj_add_flag(row[c], LV_OBJ_FLAG_CLICKABLE);
      // Tap affordance: press feedback so a tappable row reads as one.
      lv_obj_set_style_bg_color(row[c], lv_color_hex(0x66E0FF),
                                LV_STATE_PRESSED);
      lv_obj_set_style_bg_opa(row[c], LV_OPA_40, LV_STATE_PRESSED);
      lv_obj_set_user_data(row[c], (void*)(uintptr_t)i);
      lv_obj_add_event_cb(row[c], row_click_cb, LV_EVENT_CLICKED, NULL);
    }
  }
}
```

Call `ui_install_row_clicks()` immediately after `ui_apply_legibility()` in
`setup()`. The trailing `< 47` hit-test in the old draft is unnecessary — LVGL
only delivers clicks for the areas the labels cover.

> If a hit-test fallback is ever needed, do it on the **release edge** using a
> latched press point (the same `g_header_touched` pattern in
> `touchscreen_read_cb()`), not on every pressed poll.

---

### Screen 2: Protocol Detail Screen

A full-screen overlay showing the complete decoded message for one CAN-ID.

```
┌──────────────────────────────────────┐
│ ← Back                               │  ← Back button (top-left)
│                                      │
│ ── CAN ID: 0x0200 ────────────────   │
│ Protocol:  Heizung Temperature       │
│ Time:      14:32:18                  │
│ Interval:  1000 ms                   │
│                                      │
│ ── Decoded Data ──────────────────   │
│ Sensor 0:  21.50 °C                  │
│ Sensor 1:  22.30 °C                  │
│ Sensor 2:  19.75 °C                  │
│ Sensor 3:  --  (empty)               │
│                                      │
│ Raw Bytes: 50 4E 7E 5A 1F C0 00 00   │
│                                      │
│ ── Statistics ────────────────────    │
│ Frames: 142   Last: 14:32:18          │
│ Interval: 1000 ms (min 998 / max 1002)│
└──────────────────────────────────────┘
```

**✅ corrected:** A "Source: Node …" line and a "Message History" list were in
the original draft. Neither is currently available (see *Codebase Findings* 3
and 4). Show them only after the underlying fields are added, or omit them.
`Interval` **is** available today (`entry->interval_ms`). A min/max interval
would need two more fields.

**Implementation approach (programmatic overlay):**

Create the overlay **programmatically** in C++, in its own module
(`protocol_detail.cpp/.h`), rather than as a separate EEZ screen.

```cpp
static lv_obj_t* g_detail_screen = NULL;
static lv_obj_t* g_detail_lbl = NULL;   // single multi-line label is enough

static void detail_back_cb(lv_event_t* e) {
  lv_obj_add_flag(g_detail_screen, LV_OBJ_FLAG_HIDDEN);  // ✅ correct API
}

static void open_protocol_detail(uint32_t table_index) {
  if (g_detail_screen == NULL) {
    // lv_layer_top() floats above the screen and is unaffected by row updates.
    g_detail_screen = lv_obj_create(lv_layer_top());
    lv_obj_set_size(g_detail_screen, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_pos(g_detail_screen, 0, 0);
    lv_obj_set_style_bg_color(g_detail_screen, lv_color_hex(0x000000),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_detail_screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(g_detail_screen, 6, LV_PART_MAIN);
    // Resistive touch + a scrollable container = the Back tap is read as a
    // scroll and swallowed. There is nothing to scroll here.
    lv_obj_remove_flag(g_detail_screen, LV_OBJ_FLAG_SCROLLABLE);

    // Back button: a real styled button, not a bare label, so the tap target
    // is large and clearly tappable.
    lv_obj_t* back = lv_obj_create(g_detail_screen);
    lv_obj_remove_flag(back, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(back, 104, 34);
    lv_obj_set_pos(back, 6, 4);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x1E3A5F), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(back, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(back, 6, LV_PART_MAIN);
    lv_obj_set_style_border_width(back, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(back, lv_color_hex(0x66E0FF), LV_PART_MAIN);
    lv_obj_add_event_cb(back, detail_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* back_lbl = lv_label_create(back);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(back_lbl, lv_color_hex(0x9FD8FF), LV_PART_MAIN);
    lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_center(back_lbl);

    // Body text: montserrat_18. Keep the layout compact (no blank lines or
    // separator rules) so it still fits the 2.8" screen.
    g_detail_lbl = lv_label_create(g_detail_screen);
    lv_obj_set_pos(g_detail_lbl, 6, 44);
    lv_obj_set_size(g_detail_lbl, LV_HOR_RES - 12, LV_VER_RES - 50);
    lv_label_set_long_mode(g_detail_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(g_detail_lbl, lv_color_hex(0xE0E0E0),
                                LV_PART_MAIN);
    lv_obj_set_style_text_font(g_detail_lbl, &lv_font_montserrat_18,
                               LV_PART_MAIN);

    lv_obj_add_flag(g_detail_screen, LV_OBJ_FLAG_HIDDEN);
  }

  populate_detail_screen(table_index);           // fills g_detail_lbl
  lv_obj_move_foreground(g_detail_screen);        // raise above rows
  lv_obj_remove_flag(g_detail_screen, LV_OBJ_FLAG_HIDDEN);
}
```

> **✅ corrected:** the body text must be pure ASCII. The earlier draft used
> `──` (U+2500 box-drawing) separators; `montserrat_18` has no such glyph, so
> LVGL logs `glyph dsc. not found for U+2500` and paints empty boxes. Use
> `-`/labels instead.
>
> **✅ corrected:** the Back control sits at the top-left, which is inside the
> `touchscreen_read_cb()` analyzer/raw toggle band (`y ≤ 30`). While the
> detail overlay is visible, skip that band (guard it with
> `is_protocol_detail_visible()`) so tapping Back does not also flip the view
> mode.

> **✅ corrected:** The original draft used
> `lv_obj_set_style_display(obj, LV_DISPLAY_MODE_NONE, …)`. There is no such
> style property for widgets in LVGL 9; use the `LV_OBJ_FLAG_HIDDEN` flag as
> above.

The overlay is pure C++ and parented to `lv_layer_top()`, so it sits above the
row labels regardless of creation order.

**Layout per protocol type:**

| Protocol | Header Info | Decoded Section | Extra (needs new fields) |
|----------|-------------|-----------------|--------------------------|
| **HEIZUNG_ID** (0x180+rank) | rank, CAN-ID | 6-byte ROM | — |
| **HEIZUNG_TEMP** (0x200) | time, interval | 4× temperature, `--` for `INT16_MIN` | min/max |
| **HEIZUNG_PWM** (0x240/0x280) | time, interval | duty‰, frequency, ACTIVE/OFF | — |
| **HEIZUNG_ADC** (0x2C0) | time, interval | max/min/pp counts, flags | anomaly flags |
| **HEIZUNG_STATS** (0x300) | time, interval | sent/recv/refused, eflg, canstat | bus-health trend |
| **TIMESTAMP** (0x555) | — | HH:MM:SS (day) | drift vs local clock |
| **SML** (0x600/0x700) | frame count | **reassembled message** | reassembly progress |
| **TIME-BCAST** | node = CAN-ID | HH:MM:SS (day) | sync offset |

---

### Multi-Frame SML Reassembly (on detail screen)

`decode_protocol()` already accumulates SML bytes into `g_sml_buffer[]`
(`main.cpp:433-472`), but with the defects noted in *Codebase Findings*.
For the detail screen:

1. **Show the reassembled SML message** as a single logical entry, not
   fragmented rows. (In the main list, all `0x600..0x7FF` rows could collapse
   into one `SML` row pointing at the detail screen.)
2. **Display reassembly progress** while incomplete:
   ```
   SML Reassembly: 186 / 258 bytes (72%)
   Frames received: 23 / 33
   Last frame: 14:32:18
   ```
3. **When complete**, show the reassembled content. Full SML ASN.1 parsing
   would need an SML parser; at minimum show a raw hex dump of the completed
   buffer.
4. **Track reassembly per base ID.** ✅ corrected structure — key by the
   `0x600`/`0x700` base and note that `expected` includes the 2-byte length
   header (`msg_len + 2`, `main.cpp:450`):

   ```cpp
   #define SML_MAX_BYTES 258
   #define SML_MAX_MESSAGES 2   // 0x600 and 0x700

   typedef struct {
       uint16_t base_id;            // 0x600 or 0x700
       uint16_t expected_length;    // msg_len + 2
       uint16_t received_length;
       uint8_t  buffer[SML_MAX_BYTES];
       bool     active;
       bool     complete;
       time_t   last_frame_time;
       uint16_t frames_received;
   } sml_reassembly_t;

   sml_reassembly_t g_sml[SML_MAX_MESSAGES];   // non-static: exposed by the header
   ```

   Then fix the completion check to validate **before** clearing — and check
   against the *received* length, not the fixed buffer size (passing
   `SML_MAX_BYTES` only works because the header sits at offset 0; it would
   happily validate an all-zero buffer of the wrong length):
   ```cpp
   if (g_sml_received_length >= expected) {
       uint16_t saved_len = g_sml_received_length;
       bool ok = (saved_len > 16 && validate_sml_header(g_sml_buffer, saved_len));
       g_sml_active = false;
       g_sml_received_length = 0;   // clear after validation
       ...
   }
   ```

   The array must be **defined non-static** (the header exposes it to
   `protocol_detail.cpp`), and it has to be *written* from the reassembly
   path — otherwise the detail screen silently falls back to a raw hex dump.
   Mirror every update from the existing globals into the slot for that base:

   ```cpp
   int slot = ((id & ~0xFF) == CAN_ID_STROMZAEHLER_INFO_ALT) ? 1 : 0;
   if (current_offset == 0) {
       g_sml[slot].frames_received = 0;
       g_sml[slot].complete = false;
   }
   g_sml[slot].base_id        = id & ~0xFF;
   g_sml[slot].expected_length = expected;
   g_sml[slot].received_length = g_sml_received_length;
   g_sml[slot].frames_received++;
   g_sml[slot].last_frame_time = time(nullptr);
   g_sml[slot].active = true;
   ```

---

### Navigation Model

```
[Main List — Protocol Mode]
    │  (tap a row)
    ▼
[Protocol Detail Screen]   (child of lv_layer_top)
    │  (tap ← Back)
    ▼
[Main List — Protocol Mode]  ← same position, same row highlighted
```

- **No multi-screen LVGL stack required.** A single reusable overlay that is
  shown/hidden and re-populated is simpler than adding a real screen.
- To honor "same row highlighted on return", keep a `g_detail_row_index` and
  draw a highlight (e.g. a coloured border on that row's labels or an
  `lv_obj_set_style_bg_color`) when the detail closes.

---

### Data Gaps — required `can_id_entry_t` additions

| Feature shown | Field needed | Type |
|---|---|---|
| Frame count per ID | `frame_count` | `uint32_t` |
| Changed count per ID | `changed_count` | `uint32_t` |
| Interval min/max | `interval_min_ms`, `interval_max_ms` | `uint32_t` |
| Source node | `source_node` (+ IP lookup) | `uint8_t` |
| Trend/history | `int16_t history[N]` ring buffer | e.g. `[8]` |
| Last value | `last_value` | e.g. `int16_t` |

Add these to `can_id_entry_t` (`main.cpp:108`) and update them in
`can_table_update()` (`main.cpp:262`). Memory: 23 rows × a small struct is
negligible on the ESP32; the history buffers dominate and stay in
`.bss`/PSRAM.

> **Note on the 2.8" screen:** the detail overlay must fit in 320×240 logical
> pixels. Use `LV_LABEL_LONG_WRAP` (already above) and, if content overflows,
> make `g_detail_screen` scrollable rather than adding a second layout.

---

### Summary of Changes Required

| File | Change |
|------|--------|
| `screens.c` | Set the analyzer column widths in the row loop; optional row highlight styling |
| `screens.h` | No new objects required (overlay lives in its own module) |
| `main.cpp` | Call `ui_apply_columns()` + `ui_install_row_clicks()` after `ui_init()` |
| `main.cpp` | Call `ui_apply_columns(true/false)` from the mode-toggle path |
| `main.cpp` | Add `sml_reassembly_t` array and fix the SML completion check (bug #1) |
| `main.cpp` | Extend `can_id_entry_t` + `can_table_update()` with counters/interval min-max |
| `protocol_detail.cpp/.h` (new) | `open_protocol_detail()`, `populate_detail_screen()`, `ui_install_row_clicks()` |
| `haus_can.h` | (Optional) `validate_sml_header()` / SML parser for readable output |

The detail overlay and column override helper are ordinary C++ compiled into the
firmware. They are installed after `ui_init()` (and re-applied on mode toggle);
no UI tooling or code generation is involved.

---

### Visual Reference — Color Mapping

Colors are applied in `update_can_display()` (`main.cpp:637-689`, analyzer
branch) and should carry through to the detail screen.

| Protocol | CAN ID | Color (hex) | Screen |
|----------|--------|-------------|--------|
| HEIZUNG_ID | 0x180+rank | `#FFFF66` (yellow) | Row + Detail |
| HEIZUNG_TEMP | 0x200 | `#66FF66` (green) | Row + Detail |
| HEIZUNG_PWM / PWM_RPT | 0x240 / 0x280 | `#FF99FF` (pink) | Row + Detail |
| HEIZUNG_ADC | 0x2C0 | `#FFCC66` (amber) | Row + Detail |
| HEIZUNG_STATS | 0x300 | `#CCCCCC` (grey) | Row + Detail |
| TIMESTAMP | 0x555 | `#66E0FF` (cyan) | Row + Detail |
| SML / SML_ALT | 0x600 / 0x700 | `#FFC266` (orange) | Row + Detail |
| TIME-BCAST | node ID | `#E0E0E0` (white, default) | Row + Detail |
| Unknown / RAW | — | `#E0E0E0` (white, default) | Row + Detail |

---

### Open Questions / Risks

1. **Extended IDs**: `lblId` is 40-48 px; `format_can_id()` emits 8 hex chars
   (`main.cpp:504`) for extended frames, which will clip. Widen or abbreviate.
2. **UI source ownership**: `screens.c` is now treated as plain, hand-editable
   source (EEZ Studio is dropped). Keep it that way — no code generation.
3. **SML parser scope**: is a real ASN.1/SML decode in scope, or is a
   reassembled hex dump acceptable for v1?
4. **History memory budget**: decide ring-buffer depth per CAN-ID.
