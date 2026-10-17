# Display jitter investigation

Execute this document from the top. Do not skip a step. Do not add a step.
The symptom is a visible jump, tear, or shimmer of the picture when a finger
changes screens on the Waveshare panel in `ESP32/28_hauszentrale`.

You cannot see the panel. The user can. Evidence is one serial line plus one
answer line from the user. Nothing else is evidence.

## Hard rules

1. Edit only these files, and only with the patches in this document:
   - `ESP32/prj_template/include/tpl_ws7.h`
   - `ESP32/prj_template/src/tpl_ws7.cpp`
   - `ESP32/28_hauszentrale/src/main.cpp`
   - `ESP32/28_hauszentrale/src/ui/haus_ui.c`
   - `ESP32/28_hauszentrale/docs/bench/display_jitter_result.md` (the report)
2. If a patch's old text is not found, stop. Write `STOP anchor missing` in the
   report. Do not search for a similar place. Do not rewrite the patch.
3. Change one variable per flash. The probe from Phase 1 stays in place.
   Experiments E1–E4 are extra edits. Revert an experiment before applying the
   next one.
4. Do not change pixel clock, porch values, rotation, LVGL version, or the
   PlatformIO platform. Do not add a bounce buffer. Do not upgrade ESP-IDF.
   This core's RGB driver has no bounce-buffer field.
5. Do not print from the flush callback. One `PROBE` line after the tap is the
   only new log.
6. Do not run the host harness. It has no RGB DMA and cannot show this bug.
7. Do not run `make fmt`. Do not commit. Do not push.
8. At the end, restore the four source files with the git command in Phase 5.
   The report file is the only file you leave behind.
9. Do not implement a fix. The result is a conclusion code from the list in
   Phase 6.

## Closed facts

Use these as given. Do not re-investigate them.

- The panel is RGB parallel, 800×480, RGB565. The framebuffer is one buffer in
  PSRAM (`flags.fb_in_psram = 1` in `tpl_ws7_display_init`).
- Portrait mode is LVGL rotation 90. `ws7_rgb_flush_cb` rotates each strip,
  then `esp_lcd_panel_draw_bitmap` copies it into that live framebuffer.
- The installed SDK is Arduino-ESP32 2.0.17 / ESP-IDF 4.4. Its RGB driver
  restarts GDMA from the VSYNC-end interrupt every frame
  (`stream_mode`, because `relax_on_idle` is left 0). Vertical blank is about
  1 ms. `draw_bitmap` also write-backs the dirty lines through DCache.
- Touch is a GT911 polled over I2C. The SD card is SPI3 (Arduino `HSPI`),
  pins 11/12/13, chip-select on the CH422G. Those pins are not RGB pins.
  `tpl_ws7_sd_bus()` is never called.
- A screen change calls `ui_show_screen`, which deletes every widget and builds
  the next screen inside `lv_task_handler`.
- The 1 Hz UI update was already throttled because a fast refresh of this
  single buffer flickers. That is prior evidence, not a new measurement.

## Phase 0 — check anchors

Read each file. Search for the exact string. If any string is absent, stop
and write `STOP anchor missing: <file>`.

| File | Exact string |
| --- | --- |
| `ESP32/prj_template/src/tpl_ws7.cpp` | `static void ws7_rgb_flush_cb` |
| `ESP32/prj_template/src/tpl_ws7.cpp` | `esp_lcd_panel_draw_bitmap(s_rgb_panel, rot_area.x1, rot_area.y1,` |
| `ESP32/prj_template/include/tpl_ws7.h` | `int tpl_ws7_touch_points(void);` |
| `ESP32/28_hauszentrale/src/main.cpp` | `lv_task_handler();` |
| `ESP32/28_hauszentrale/src/main.cpp` | `if (haus_sd_mount()) {` |
| `ESP32/28_hauszentrale/src/main.cpp` | `lv_indev_set_cursor(indev, cursor);` |
| `ESP32/28_hauszentrale/src/ui/haus_ui.c` | `static void ui_show_screen(int screen)` |
| `ESP32/28_hauszentrale/src/ui/haus_ui.c` | `lv_obj_set_style_bg_opa(s_zone[i], 32, LV_STATE_PRESSED);` |

## Phase 1 — install the probe

Apply the four patches below. Do not edit anything else.

### Patch A — `ESP32/prj_template/include/tpl_ws7.h`

Find:

```
int tpl_ws7_touch_points(void);

#endif // IS_WAVESHARE_LCD7
```

Replace with:

```
int tpl_ws7_touch_points(void);

#ifdef __cplusplus
extern "C" {
#endif
void ws7_probe_arm(int screen);
void ws7_probe_mark_built(uint32_t build_ms);
void ws7_probe_finish(void);
#ifdef __cplusplus
}
#endif

#endif // IS_WAVESHARE_LCD7
```

### Patch B — `ESP32/prj_template/src/tpl_ws7.cpp`

Add `#include <esp_timer.h>` next to the other includes.

Find `static uint8_t *s_rot_buf = NULL;` and insert this block after it:

```cpp
static volatile int s_probe_screen = -1;
static volatile uint32_t s_probe_arm_ms = 0;
static volatile uint32_t s_probe_build_ms = 0;
static volatile uint32_t s_probe_flushes = 0;
static volatile uint32_t s_probe_bytes = 0;
static volatile uint32_t s_probe_lines = 0;
static volatile uint32_t s_probe_max_us = 0;

static void ws7_probe_count_flush(uint32_t bytes, uint32_t lines, int64_t t0) {
  if (s_probe_screen < 0) {
    return;
  }
  int64_t dt = esp_timer_get_time() - t0;
  if (dt < 0) {
    dt = 0;
  }
  s_probe_flushes++;
  s_probe_bytes += bytes;
  s_probe_lines += lines;
  if ((uint32_t)dt > s_probe_max_us) {
    s_probe_max_us = (uint32_t)dt;
  }
}

extern "C" void ws7_probe_arm(int screen) {
  s_probe_flushes = 0;
  s_probe_bytes = 0;
  s_probe_lines = 0;
  s_probe_max_us = 0;
  s_probe_build_ms = 0;
  s_probe_arm_ms = millis();
  s_probe_screen = screen;
}

extern "C" void ws7_probe_mark_built(uint32_t build_ms) {
  s_probe_build_ms = build_ms;
}

extern "C" void ws7_probe_finish(void) {
  if (s_probe_screen < 0) {
    return;
  }
  uint32_t age = millis() - s_probe_arm_ms;
  if (s_probe_flushes == 0 && age < 300) {
    return;
  }
  int screen = s_probe_screen;
  uint32_t build_ms = s_probe_build_ms;
  uint32_t flushes = s_probe_flushes;
  uint32_t bytes = s_probe_bytes;
  uint32_t lines = s_probe_lines;
  uint32_t max_us = s_probe_max_us;
  s_probe_screen = -1;
  TPL_LOGI("probe",
           "PROBE screen=%d build_ms=%u flushes=%u bytes=%u lines=%u "
           "max_flush_us=%u",
           screen, (unsigned)build_ms, (unsigned)flushes, (unsigned)bytes,
           (unsigned)lines, (unsigned)max_us);
}
```

Replace the whole function `ws7_rgb_flush_cb` with:

```cpp
static void ws7_rgb_flush_cb(lv_display_t *disp, const lv_area_t *area,
                             uint8_t *px_map) {
  if (!s_rgb_panel) {
    lv_display_flush_ready(disp);
    return;
  }

  int64_t t0 = esp_timer_get_time();
  lv_display_rotation_t rot = lv_display_get_rotation(disp);
  if (rot == LV_DISPLAY_ROTATION_0 || s_rot_buf == NULL) {
    esp_lcd_panel_draw_bitmap(s_rgb_panel, area->x1, area->y1, area->x2 + 1,
                              area->y2 + 1, px_map);
    ws7_probe_count_flush((uint32_t)lv_area_get_width(area) *
                              (uint32_t)lv_area_get_height(area) * 2,
                          (uint32_t)lv_area_get_height(area), t0);
    lv_display_flush_ready(disp);
    return;
  }

  lv_area_t rot_area = *area;
  lv_display_rotate_area(disp, &rot_area);

  int32_t src_w = lv_area_get_width(area);
  int32_t src_h = lv_area_get_height(area);
  int32_t dst_w = lv_area_get_width(&rot_area);

  lv_draw_sw_rotate(px_map, s_rot_buf, src_w, src_h, src_w * 2, dst_w * 2, rot,
                    LV_COLOR_FORMAT_RGB565);

  esp_lcd_panel_draw_bitmap(s_rgb_panel, rot_area.x1, rot_area.y1,
                            rot_area.x2 + 1, rot_area.y2 + 1, s_rot_buf);
  ws7_probe_count_flush((uint32_t)src_w * (uint32_t)src_h * 2, (uint32_t)src_h,
                        t0);

  lv_display_flush_ready(disp);
}
```

### Patch C — `ESP32/28_hauszentrale/src/main.cpp`

Find:

```
  /* Process LVGL tasks (display flush, input processing, animations) */
  lv_task_handler();

  vTaskDelay(5 / portTICK_PERIOD_MS);
```

Replace with:

```
  /* Process LVGL tasks (display flush, input processing, animations) */
  lv_task_handler();
  ws7_probe_finish();

  vTaskDelay(5 / portTICK_PERIOD_MS);
```

### Patch D — `ESP32/28_hauszentrale/src/ui/haus_ui.c`

Replace the whole function `ui_show_screen` with this. The `switch` body
stays as it is in the file today. The only added lines are the `HAUS_HOST`
block at the top and the `ws7_probe_mark_built` call at the bottom.

```c
static void ui_show_screen(int screen) {
  if (screen < UI_SCREEN_HOME || screen > UI_SCREEN_LOG)
    return;
#ifndef HAUS_HOST
  extern void ws7_probe_arm(int screen);
  extern void ws7_probe_mark_built(uint32_t build_ms);
  ws7_probe_arm(screen);
  uint32_t probe_t0 = (uint32_t)lv_tick_get();
#endif
  g_current_screen = screen;

  haus_ui_deinit();

  switch (screen) {
  case UI_SCREEN_HOME:
    haus_ui_init();
    break;
  case UI_SCREEN_CHAMBER:
    haus_ui_build_detail();
    break;
  case UI_SCREEN_WATER_IN:
  case UI_SCREEN_WATER_OUT:
    haus_ui_build_water(screen);
    break;
  case UI_SCREEN_POWER:
    haus_ui_build_power();
    break;
  case UI_SCREEN_LIVING:
    haus_ui_build_living();
    break;
  case UI_SCREEN_SETTINGS:
    haus_ui_build_settings();
    break;
  case UI_SCREEN_LOG:
    haus_ui_build_log();
    break;
  }
#ifndef HAUS_HOST
  ws7_probe_mark_built((uint32_t)lv_tick_get() - probe_t0);
#endif
}
```

### Compile

From `ESP32/28_hauszentrale`:

```bash
pio run -e esp32usb
```

If it fails, paste the first compiler error into the report as
`STOP build failed` and stop. Do not "fix" warnings by changing unrelated
code. A failure in the probe you just added: re-read the patch and correct
only a typo that the error names. One retry. Then stop.

## Phase 2 — flash and ask

Flash once:

```bash
cd ESP32/28_hauszentrale && pio run -e esp32usb --target upload
```

If the upload fails, stop. Write `STOP upload failed` and the last 20 lines
of the tool output. Do not switch to `esp32ota` unless the user has already
replied with the single token `USE_OTA`.

Start a serial capture at 115200 baud and keep it running. Then send the user
this text, unchanged:

```
Panel is on the home screen. Tap the heater picture once, so it opens the
chamber screen. Do not use three fingers. Wait two seconds.

Reply with one line, nothing else:
VISUAL <WHEN> <SHAPE>

WHEN is one of: FINGER  SWITCH  BOTH  NONE
SHAPE is one of: SHIFT  BANDS  WIPE  SPECK  NONE

FINGER = the picture misbehaves while your finger is down, before the new
screen is up.
SWITCH = the picture misbehaves as the new screen appears.
BOTH = both of those.
NONE = the picture stays still.
SHIFT = the whole picture jumps.
BANDS = horizontal bars.
WIPE = the new screen paints across in a sweep.
SPECK = only a small spot near the finger.
```

Wait for the user's line. Copy the first serial line that contains
`PROBE screen=` after you asked. If there is no such line, ask the user to
tap once more. If the second tap also produces no line, stop with
`STOP no probe line`.

## Phase 3 — classify the baseline

Parse the probe line as six integers: `screen`, `build_ms`, `flushes`,
`bytes`, `lines`, `max_flush_us`.

Classify bytes with these tests, in order:

| Test | Class |
| --- | --- |
| `flushes == 0` | `NO_FLUSH` |
| `bytes >= 500000` | `FULL` |
| `bytes >= 1000` | `SMALL` |
| else | `TINY` |

Parse the user line. It must be two tokens from the lists in Phase 2.
If it is not, ask once for the same line. If the second reply is also not
two legal tokens, stop with `STOP bad visual`.

Follow this table. Do not combine rows.

| Visual WHEN | Probe class | Next |
| --- | --- | --- |
| `NONE` | any | Stop. Conclusion `NOT_REPRODUCED`. |
| any except `NONE` | `NO_FLUSH` | Stop. Conclusion `PROBE_BROKEN`. |
| `SWITCH` or `BOTH` | `FULL` or `SMALL` | Run E1. |
| `FINGER` | any except `NO_FLUSH` | Skip E1. Run E3. |
| `SWITCH` or `BOTH` | `TINY` | Run E1. |

Record the baseline row in the report before you flash again.

## Phase 4 — experiments

Each experiment starts from the Phase 1 tree plus the one patch named here.
Flash `esp32usb` again. Ask the user with the script named here. Capture one
new `PROBE screen=` line. Then revert that experiment's patch before the next
experiment.

### E1 — do not rebuild the screen

The `return` must run before `haus_ui_deinit()`. A return after the `switch`
does not skip the rebuild.

In `haus_ui.c`, find:

```c
  ws7_probe_arm(screen);
  uint32_t probe_t0 = (uint32_t)lv_tick_get();
#endif
  g_current_screen = screen;

  haus_ui_deinit();
```

Replace with:

```c
  ws7_probe_arm(screen);
  uint32_t probe_t0 = (uint32_t)lv_tick_get();
  (void)probe_t0;
  ws7_probe_mark_built(0);
  return;
#endif
  g_current_screen = screen;

  haus_ui_deinit();
```

Leave the `switch` and the later `ws7_probe_mark_built` call in the file.
On the device the `return` skips them. They must still compile.

User script for E1 (send unchanged):

```
This run will NOT change screens. Tap the heater picture once. Wait two
seconds. Reply with one line:
VISUAL <WHEN> <SHAPE>
Use the same WHEN and SHAPE words as last time.
```

Revert E1 before doing anything else: remove the three added lines
`(void)probe_t0;`, `ws7_probe_mark_built(0);`, and `return;`.

A press can still flush the heater zone (about 120 KB) plus the cursor.
That is not the screen rebuild. Call E1 `COPY_STOPPED` only when all of
these are true:

- E0 class is `FULL`
- E1 class is `NO_FLUSH`, `TINY`, or `SMALL`
- `E1.bytes < 200000`
- `E1.bytes * 2 < E0.bytes`

Otherwise call E1 `COPY_STILL`.

| E1 WHEN | E1 copy | Next |
| --- | --- | --- |
| `NONE` or `FINGER` | `COPY_STOPPED` | The screen-change glitch needs the rebuild. Do not run E2. If the baseline WHEN was `BOTH` or the E1 WHEN is `FINGER`, run E3. Otherwise stop with `REDRAW_OF_NEW_SCREEN`. |
| `SWITCH` or `BOTH` | `COPY_STOPPED` | The picture still jumps without the rebuild copy. Run E2. |
| any | `COPY_STILL` | The skip did not stop the copy. Stop with `STOP e1 still flushed`. |

### E2 — do not mount the SD card

Run E2 only when the E1 table says so.

In `main.cpp`, find:

```
  if (haus_sd_mount()) {
    haus_sd_selftest();
  } else {
```

Replace with:

```
  if (0 && haus_sd_mount()) {
    haus_sd_selftest();
  } else {
```

User script: the Phase 2 script (the screen is supposed to change).

Revert the `if (0 && haus_sd_mount())` back to `if (haus_sd_mount())` before
the next experiment.

E2 decision:

| E2 WHEN is `NONE` or `FINGER`, and baseline WHEN was `SWITCH` or `BOTH` | Conclusion `SD_SPI_REQUIRED`. Stop. |
| E2 WHEN is still `SWITCH` or `BOTH` | SD mount is not required. Conclusion `NOT_THE_REDRAW`. Stop. |

### E3 — no cursor

Run E3 only when the baseline or E1 table says so.

In `main.cpp`, find `lv_indev_set_cursor(indev, cursor);` and delete that
single line.

Find:

```
  if (s_touch_cursor && s_touch_indev) {
```

Replace with:

```
  if (0 && s_touch_cursor && s_touch_indev) {
```

User script: the Phase 2 script if the baseline navigation still happens
(E1 was reverted). If you were sent here from a baseline of `FINGER` only,
use the Phase 2 script as well. The screen is supposed to change.

Revert both E3 edits before the next experiment.

E3 decision:

| Baseline WHEN | E3 WHEN | Next |
| --- | --- | --- |
| `FINGER` | `NONE` | Conclusion `PRESS_OR_CURSOR_ONLY`. Run E4 to split press tint from cursor. |
| `FINGER` | `FINGER` | Cursor is not required. Run E4. |
| `BOTH` | `SWITCH` | Cursor caused the finger part. The switch part is already classified by E1. Stop. Use the E1 conclusion. |
| `BOTH` | `BOTH` or `FINGER` | Run E4. |
| `SWITCH` | any | You should not be in E3. Stop with `STOP bad branch`. |

### E4 — no press tint

Run E4 only when the E3 table says so.

In `haus_ui.c`, find:

```
    lv_obj_set_style_bg_opa(s_zone[i], 32, LV_STATE_PRESSED);
```

Replace with:

```
    /* probe E4: press tint removed */
```

User script: the Phase 2 script.

Revert by putting the `lv_obj_set_style_bg_opa` line back.

E4 decision:

| E3 result | E4 WHEN | Conclusion |
| --- | --- | --- |
| E3 WHEN was `NONE` | do not use E4's WHEN for the cursor; E3 already cleared it | `PRESS_OR_CURSOR_ONLY` with note `cursor` |
| E3 WHEN was still `FINGER` | `NONE` | `PRESS_OR_CURSOR_ONLY` with note `press_tint` |
| E3 WHEN was still `FINGER` | `FINGER` | `NOT_THE_REDRAW` |
| E3 WHEN was `BOTH` or `FINGER` after a `BOTH` baseline | `SWITCH` | Finger part was the press tint. Switch part follows E1. |
| E3 WHEN was `BOTH` or `FINGER` after a `BOTH` baseline | `BOTH` or `FINGER` | `NOT_THE_REDRAW` |

If E1's conclusion was `REDRAW_OF_NEW_SCREEN` and E4 only explains the finger
part, the report conclusion is `REDRAW_OF_NEW_SCREEN` and the note records
the finger part separately. Do not drop the redraw conclusion.

## Phase 5 — restore the tree

Run:

```bash
cd /Users/jochen/src/ESP32_projects
git checkout -- \
  ESP32/prj_template/include/tpl_ws7.h \
  ESP32/prj_template/src/tpl_ws7.cpp \
  ESP32/28_hauszentrale/src/main.cpp \
  ESP32/28_hauszentrale/src/ui/haus_ui.c
```

Then `git status --short`. Those four paths must not appear. If they appear,
stop with `STOP dirty tree` and do not delete unrelated changes.

## Phase 6 — write the report

Write `ESP32/28_hauszentrale/docs/bench/display_jitter_result.md` with this
shape. Fill every field. Use `n/a` when an experiment did not run. Copy the
probe line and the visual line exactly. Do not paraphrase them.

```markdown
# Display jitter result

Conclusion: <one code from the list below>
Note: <cursor | press_tint | none | n/a>

## Runs

| Run | Probe line | Visual line | Class |
| --- | --- | --- | --- |
| E0 baseline | | | |
| E1 no rebuild | | | |
| E2 no SD mount | | | |
| E3 no cursor | | | |
| E4 no press tint | | | |

## What this separates

- REDRAW_OF_NEW_SCREEN means the glitch on screen change happens only when
  ui_show_screen builds and flushes the new screen. It does not separate a
  PSRAM-bandwidth stall from a late VSYNC restart. Both happen inside
  draw_bitmap on this IDF 4.4 driver. No further software experiment from
  this document separates them.
- SD_SPI_REQUIRED means skipping haus_sd_mount removed the screen-change
  glitch.
- PRESS_OR_CURSOR_ONLY means a press with no screen-change redraw was enough,
  and E3 or E4 removed it.
- NOT_THE_REDRAW means the picture still jumped when the rebuild was skipped
  and the SD mount was skipped, or the finger glitch survived E3 and E4.
- NOT_REPRODUCED, PROBE_BROKEN, and any STOP line mean the run is incomplete.

## Not done

No fix was applied. The four source files were restored.
```

Legal conclusion codes, and no others:

- `NOT_REPRODUCED`
- `PROBE_BROKEN`
- `REDRAW_OF_NEW_SCREEN`
- `SD_SPI_REQUIRED`
- `PRESS_OR_CURSOR_ONLY`
- `NOT_THE_REDRAW`
- `STOP anchor missing`
- `STOP build failed`
- `STOP upload failed`
- `STOP no probe line`
- `STOP bad visual`
- `STOP e1 still flushed`
- `STOP bad branch`
- `STOP dirty tree`

When the conclusion is `REDRAW_OF_NEW_SCREEN`, add these two sentences to the
report and then stop:

```
Next measurement, not part of this run: a GPIO toggled at the start of the
RGB VSYNC ISR, compared with VSYNC on GPIO 3, during one chamber tap.
Late restarts mean the ISR missed the 1 ms blank. On-time restarts mean the
shift is the in-frame PSRAM copy.
```

Do not perform that measurement. Do not edit the IDF package.
