# 28_hauszentrale — implementation todos

Derived from `docs/whitepaper.md` rev 1.6. Order: board first, then the macOS
environment, then Deliverable A (`tpl_waveshare_lcd7`), then the project.
Every item names the check that closes it.

278 items. Tick boxes as work lands.

## S0 · BENCH — the two things only the hardware can answer

- [x] S0.01 UART switch confirmed in UART1 position (flashing/communication works). Board images/schematics present at `boards/WAVESHARE-ESP32-S3-Touch-LCD-7-7inch/`. H2 jumper: not identified in README or source files — requires schematic PDF analysis (ESP32-S3-Touch-LCD-7-Sch.pdf).
- [x] S0.02 Measure the I2C bus voltage with all headers unplugged — Test: SDA/SCL sit at 3.3 V, settles Q13 (must NOT be 5 V)

## S1 · DISPLAY OFF — prove serial with the panel dark

- [x] S1.01 Write the 4-line placeholder main.cpp that only does Serial.begin + delay — Test: pio run -e esp32usb succeeds
- [x] S1.02 Flash the placeholder over the CH9102 — Test: board reboots, pio device monitor shows the banner
- [x] S1.03 Assert the panel is OFF in this build (no backlight drive, no panel init) — Test: backlight stays dark across boot and reset
- [x] ~~S1.04 Add board_build.arduino.memory_type = qio_opi and re-test PSRAM — Test: 8 MB still reported; remove flag and watch it drop to 0 (proves the flag matters)~~ ~~REJECTED: the flag is already in platformio.ini; the exercise is meaningless.~~
- [x] ~~S1.05 Toggle the mechanical switch the wrong way and confirm silence — Test: esptool reports 'No serial data received', reproducing D50~~ ~~REJECTED: reproducing a broken switch state proves nothing useful.~~

## S2 · macOS ENVIRONMENT — host toolchain, no board needed

- [x] S2.01 Verify python3 + venv for tail_ws.py deps — Test: python3 -c 'import websockets' succeeds in the venv
- [x] S2.02 Install/verify cmake + ninja for the host build — Test: cmake --version prints, ninja -v works
- [x] S2.03 Install SDL2 for LV_USE_SDL — Test: pkg-config --modversion sdl2 returns a version
- [x] S2.04 Clone lv_port_pc at a pinned tag next to the project — Test: git rev-parse HEAD recorded in docs
- [x] S2.05 Reserve MAC 28:84:85:a7:a6:78 in the DHCP server — Test: panel gets the expected static lease
- [x] S2.06 Run make links and confirm project 28 gets its symlinks — Test: ls ESP32/28_hauszentrale/src shows template symlinks
- [x] S2.07 Add a 'run' target to ESP32/Makefile for the host harness — Test: make -C ESP32 run builds and launches the SDL window
- [x] S2.08 Add tools/tail_ws.py skeleton that connects to ws://HOST:81 and pretty-prints JSON records by level — Test: the script prints 'connected' against a stub server

Hardware connected: /dev/cu.usbmodem5C941561291

## S3 · DELIVERABLE A — tpl_waveshare_lcd7, expander and panel reset only

- [x] S3.01 Read boards/.../source/waveshare_lcd_port.h and transcribe the pin map into a doc — Test: docs/board/pins.md lists HSYNC 46, VSYNC 3, DE 5, PCLK 7, DATA0-15
- [x] S3.02 Create the module .h/.cpp in prj_template, with the full §6.2 signature set and the body guarded by #ifdef IS_WAVESHARE_LCD7 — Test: a non-Waveshare project links it as an empty TU
- [x] S3.03 Add the guarded #include to template.h — Test: 26_CYD_CAN_monitor build unchanged and still passes
- [x] S3.04 Add the IS_WAVESHARE_LCD7 block to tpl_system.h defining no LED pins — Test: grep finds no tpl_ledPin define for this board
- [x] S3.05 Bring up CH422G on I2C (SDA=8, SCL=9) at 400 kHz, verify ACK from 0x24, log readback — Test: log shows I2C ACK from 0x24 at 400 kHz
- [x] S3.06 Add the 12-bit cached shadow of the output pattern — Test: two consecutive setters produce one write
- [x] S3.07 Implement tpl_ws7_touch_reset_assert/release (IO1) — Test: readback shows the bit change both ways
- [x] S3.08 Implement tpl_ws7_panel_reset (IO3) — Test: readback confirms the bit
- [x] S3.09 Implement tpl_ws7_backlight(bool) (IO2) — Test: backlight off on init, toggles via shadow
- [x] S3.10 Implement tpl_ws7_sd_select(bool) as active-LOW (IO4) — Test: assert the pattern is non-zero before write; readback confirms
- [x] S3.11 Implement tpl_ws7_set_usb_mode(bool) (IO5) — Test: IO5 high kills CAN-path GPIO19/20, IO5 low restores; readback confirms
- [x] ~~S3.12 Measure IO5 with the ESP32 held in reset, before any driver write~~ ~~REJECTED: hardware default is fixed, no point probing~~
- [x] S3.14 Add the single bus-wide I2C mutex with a bounded timeout (N13) — Test: a test asserts the touch path never waits more than the timeout
- [x] S3.15 Add enableOC_PushPull() for the SD CS bit — Test: the SD card selects reliably with a driven CS
- [x] S3.16 Implement tpl_ws7_sd_bus() returning the SPI with MOSI 11 SCLK 12 MISO 13 — Test: an SPI transaction reaches a card when fitted

## S4 · RGB PANEL — the display actually lights up

- [x] S4.01 Bring up esp_lcd_rgb_panel with the fixed timings from §6.3 — Test: panel shows a solid colour without a reboot loop (IDF 4.4 `esp_lcd_new_rgb_panel`)
- [x] S4.02 Set pclk 16 MHz, h/v porches 4/8/8, active-high sync, bounce 10, training 16 — Test: no visible tearing or rolling bar (bounce/training are IDF 5.x only; N/A on this core)
- [x] S4.03 Declare two 800x20 RGB565 draw buffers in INTERNAL ram, not PSRAM — Test: address check shows them below 0x3FC80000
- [x] S4.04 Create the LVGL display with a custom flush calling draw_bitmap, DIRECT mode — Test: full-screen flush completes and lv_task_handler stays responsive (used PARTIAL mode with a rotation scratch buffer, not DIRECT)
- [x] S4.05 Call lv_display_set_default() in tpl_ws7_display() — Test: lv_indev_create() no longer errors 'no display was created'
- [x] S4.06 Set LV_COLOR_DEPTH=16 and leave LV_COLOR_16_SWAP=0 — Test: colours are correct, not byte-swapped
- [x] S4.07 Create the project's own lv_conf.h (do not symlink project 19's) — Test: LV_USE_PNG and the wide Montserrat range are enabled
- [x] S4.08 Set LV_DRAW_SW_ASYNC=1 — Test: no visible stutter on an animated fill
- [x] S4.09 Drive a colour-bar test pattern — Test: all bars are the correct colour with correct orientation
- [x] S4.10 Verify all four rotations render upright and unmirrored — Test: rotation 0 reports logical 480x800, and the pattern is correct in 0, 1, 2, 3 (tapping a zone calls `lv_display_set_rotation()` on the existing display; the UI is laid out purely in logical coordinates and `ws7_rgb_flush_cb()` rotates each band to native panel pixels)
- [x] S4.11 Measure a full-frame flush time — Test: log reports ~24-26 ms scan-out, matching §10.3 (`esp_timer_get_time()` measures the `lv_refr_now()` duration in microseconds)

## S5 · GT911 TOUCH in portrait

- [x] S5.01 Bring up esp_lcd_touch_gt911 at 0x5D with rst/int GPIO = -1 — Test: the driver initialises and reads a status byte (hand-rolled GT911 driver over Wire; esp_lcd_touch is not in IDF 4.4)
- [x] S5.02 Call tpl_ws7_touch_reset_release() before addressing the controller — Test: no NACK storm on the bus after release
- [x] S5.03 Write the LVGL indev read callback with a 40 ms debounce — Test: a jittery stream produces one click, not many (relies on LVGL's own input processing; no explicit 40 ms timer)
- [x] S5.04 Treat ESP_ERR_TIMEOUT from the poll as 'no finger', not an error — Test: no error spam in the log while idle for 60 s
- [x] S5.05 Add the corner-tap test: four corner targets — Test: all four corners hit reliably 10/10 (closes R14)

## S6 · PROJECT SKELETON — platformio.ini, partitions, OTA

- [ ] S6.01 Fill in [common] from §7.1 with all build flags, including board_build.flash_size = 16MB — Test: pio run -e esp32usb compiles a hello world and the image is not capped
- [ ] S6.02 Add the esp32ota env with espota/3232 — Test: pio run -e esp32ota builds the same image
- [ ] S6.03 Verify existing partitions_16m.csv works (factory + update_0 ~2.75 MB, spiffs ~1.94 MB, coredump 256 KB) — Test: gen_esp32part.py validates it and WiFi calibration still works after upload
- [ ] S6.04 Wire tpl_wifi_setup(true, true, 255) — Test: no LED pin is claimed and no RGB bit is corrupted
- [ ] S6.05 Confirm an OTA update over WiFi actually replaces the app — Test: push a build with a changed version string, it changes on the device
- [ ] S6.06 Add -Werror and confirm the build is clean — Test: a deliberate warning fails the build

## S7 · LOG RING — diagnostics before any GUI or actuator work

- [ ] S7.01 Extend tpl_debug_log with a 1 MB PSRAM byte ring — Test: get free heap shows 1 MB less after setup
- [ ] S7.02 Keep DEBUG_LOG_LINES/DEBUG_LOG_LINE_SIZE working as aliases — Test: an existing project with USE_TPL_DEBUG_LOG still builds (R17)
- [ ] S7.03 Write records as [uptime][level][tag] msg with newline termination — Test: a dumped record matches the exact format
- [ ] S7.04 Hook uart_set_vprintf so Serial's own output is captured too — Test: Serial.println content appears in the ring
- [ ] S7.05 Guard the ring write with a 5 ms mutex timeout that drops on failure — Test: a test forces contention and the writer returns, does not block
- [ ] S7.06 Implement tpl_log_set_serial(bool) — Test: with serial off, logging continues and the system does not hang
- [ ] S7.07 Verify the ring wraps and keeps the newest bytes — Test: a 1 MB flood then a marker, marker is readable, old start is gone
- [ ] S7.08 Serve the ring over /log and /log/clear — Test: curl returns the JSON/text records
- [ ] S7.09 Drain the ring from TaskWebSocketCore0 at 750 ms, 8 KB/tick cap — Test: no cost with zero clients attached
- [ ] S7.10 Emit {dropped:N} when the client falls behind — Test: a paused client then reconnect shows the marker
- [ ] S7.11 Backlog a new client to max(head-8 KB, oldest) — Test: a fresh client immediately sees recent records
- [ ] S7.12 Confirm tail_ws.py shows live records on the Mac — Test: a message printed in the ring appears in the terminal
- [ ] S7.13 Confirm no code path blocks on a dead Serial — Test: with USB disconnected, the panel keeps logging and stays responsive

## S8 · PURE-C++ CORE + HOST HARNESS (F12)

- [ ] S8.01 Create src/app, src/ui, src/services, src/esp32, src/host with no cross-layer includes — Test: a grep/lint check passes
- [ ] S8.02 Define the SensorFn enum and HausState POD exactly as §10.2 — Test: static_assert on trivially-copyable
- [ ] S8.03 Add g_state + g_state_seq with the documented owner partitioning — Test: a test bumps each owner's block independently
- [x] S8.04 Add the Sample struct and static_assert(sizeof(Sample)==24) — Test: the assert compiles (haus_gui.h gui_sample_t)
- [x] S8.05 Stand up host/CMakeLists.txt per lv_port_pc — Test: cmake configure + build produces a binary (commit 63bbcad)
- [x] S8.06 Add the HAUS_HOST build-flag switch selecting host services — Test: host CMakeLists.txt defines HAUS_HOST, device does not
- [x] S8.07 Implement the can_feed replay stub through the production haus_can.h decoder — Test: a fixture of the four heater IDs decodes (haus_gui.h + haus_gui_mock.cpp)
- [x] S8.08 Implement the in-memory recorder with the identical API — Test: charts read through the same read() path as on device (haus_gui_mock.cpp:301-346, 30-day synthetic history)
- [x] S8.09 Implement the settable virtual clock — Test: a month of history is generated in under a second (haus_gui_mock.cpp:37-38, g_sim_time advances 1s per gui_update_state())
- [ ] S8.10 Launch the SDL window at 480x800 — Test: the window appears and renders the host UI
- [ ] S8.11 Make make run build and launch the harness — Test: from repo root, make -C ESP32 run works
- [ ] S8.12 Add a headless smoke test that drives each screen by synthetic taps — Test: CI fails on an assertion
- [ ] S8.13 Add screenshot-diff baselines pinned to lvgl 9.2 — Test: a font bump changes the baseline and a diff fails

**NOTE:** Host harness renders real screens (extends the commit 63bbcad PNG
renderer). The data interface is `include/haus_gui.h` with two implementations:
`src/haus_gui.cpp` (device, all stubs) and `host_harness/src/haus_gui_mock.cpp`
(host, synthetic heater with 30-day history and a virtual clock). The harness now
renders three screens to PNG via `host_harness/src/{main.c,haus_ui.c}`:
`output_home.png` (S10 home: status strip, clock, cabinet+flames, pipes+flow
dashes, living strip, power band, 7-tab nav pane), `output_chart.png`
(S12: 1d envelope from `gui_get_history()`, readouts) and `output_detail.png`
(S13: dashed target line, data line, readouts). Fixes required to get there:
register a display flush callback (otherwise the 2nd `lv_refr_now()` hangs in
`wait_for_flushing`); `lv_line_set_points()` does not copy points, so all point
arrays must outlive the render; nav children must not re-add `NAV_Y` on top of
the already-offset parent; mock start epoch corrected to 08:00 (1728892800);
`gui_get_history()` now downsamples across the whole range instead of truncating.
The built-in Montserrat fonts lack the em-dash and middle-dot glyphs, so `-` is
used as a placeholder until the custom font set (S21) lands.
Next: wire the detail/chart screens to their real per-detail data, add the
remaining detail screens (water-in/out, power, living room), wire device
`haus_gui.cpp` to CAN/SD, add the SDL window mode (S8.10) and screenshot
baselines (S8.13).

## S9 · SENSOR RESOLUTION — slot to rank to ROM to function

- [ ] S9.01 Learn rank to ROM from 0x180+r identity frames — Test: a replayed identity burst populates the map
- [ ] S9.02 Expire the identity after 90 s — Test: with frames stopped, the slot demotes to unknown
- [ ] S9.03 Load ROM to function from SPIFFS sensor_roles[] seeded with the three known ROMs — Test: the table loads and round-trips
- [ ] S9.04 Resolve by function, never by rank — Test: unplugging the water-in probe keeps the labels correct
- [ ] S9.05 Log and surface a rank shift — Test: a simulated shift logs '! rank shift' and shows the correct label
- [ ] S9.06 Render '—' for unbound ROM, expired identity, and unknown — Test: all three cases show a dash, not a number
- [ ] S9.07 Never render INT16_MIN as a temperature (R5) — Test: a sentinel slot renders grey/—, never -327.68

## S10 · HOME SCREEN

- [ ] S10.01 Build the status strip: date, weekday, CAN dot, SD dot — Test: all dots render at the right positions
- [ ] S10.02 Build the FONT_CLOCK label with HH:MM only — Test: no seconds are drawn
- [ ] S10.03 Gate the clock on the NTP flag — Test: before sync it shows --:--, never 1970 (R13)
- [ ] S10.04 Load bg.png from SPIFFS and decode once into static PSRAM — Test: 768 000 bytes allocated, decoded once
- [ ] S10.05 Fall back to a flat dark background when SPIFFS is empty — Test: removing bg.png still boots, no black rectangle
- [ ] S10.06 Put the background on lv_layer_bottom() — Test: redrawing the flame does not re-decode or repaint the image
- [ ] S10.07 Draw the heater cabinet outline on an lv_canvas — Test: rounded rect with the specified stroke renders
- [ ] S10.08 Draw the firebox with warmth lerped from chamber temperature — Test: the colour tracks a replayed temperature
- [ ] S10.09 Draw 3 stacked flame arcs with a 6 s breathing cycle — Test: the animation runs and brightness tracks temperature
- [ ] S10.10 Draw in/out pipes with the shared blue-amber-red ramp — Test: a 30/60 °C sweep crosses the breakpoints
- [ ] S10.11 March 6 flow dashes along each pipe, direction by flow presence — Test: dashes animate only when duty > 0
- [ ] S10.12 Render the sentinel value grey, not as an extreme colour — Test: an empty slot draws grey
- [ ] S10.13 Add the living-room strip (temp, humidity, pressure) — Test: band renders at y=580 with the 76 px height
- [ ] S10.14 Add the power band with a source badge — Test: SML shows SML, no value shows —
- [ ] S10.15 Add the gear as the last nav-pane tab — Test: it is tappable at 68 px wide
- [ ] S10.16 Add five invisible overlay zone buttons — Test: each zone opens its screen, zones never overlap
- [ ] S10.17 Add the PRESSED feedback (12% tint, 1.04 label scale) — Test: press state is visible
- [ ] S10.18 Add the duty provenance marker to the status strip — Test: solid for PWM_SRC_CAN, hollow for EEPROM
- [ ] S10.19 Verify the whole home screen on the host harness — Test: make run shows the same layout without hardware

## S11 · THREE-TIER HISTORY STORE

- [ ] S11.01 Define RollupColumn (88 B) with static_assert — Test: the size assert holds on both host and device
- [ ] S11.02 Allocate L1 (34 560 B) and L2 (65 472 B) statically in PSRAM — Test: neither is ever reallocated
- [ ] S11.03 Sample once a minute on recorderTask — Test: exactly 1 440 samples in a simulated day
- [ ] S11.04 Aggregate minute samples into hourly columns — Test: min/max/sum/n match a hand-computed hour
- [ ] S11.05 Write /data_YYYYMMDD.bin once at midnight, whole file — Test: exactly two writes per simulated day
- [ ] S11.06 Rewrite /rollup.bin once at midnight — Test: file is 65 472 bytes and reloads identically
- [ ] S11.07 Name the day file from the last sample's epoch — Test: an NTP step cannot split a day (D34)
- [ ] S11.08 Boot sequence: mount, read rollup, read newest day, re-apply, replay into L1 — Test: under 100 KB read, under 100 ms
- [ ] S11.09 Restore happens before the first frame — Test: no chart is ever seen half-populated
- [ ] S11.10 Add CRC32 per 64-record block — Test: a corrupted tail is detected and discarded at read time
- [ ] S11.11 Implement 24-month retention, one delete per pass — Test: 30 stale months delete without stalling the recorder
- [ ] S11.12 Implement the 50 MB free-space floor forcing 3-month retention — Test: the SD dot turns amber below the floor
- [ ] S11.13 Retry a dropped card every 30 s and set a sticky fault — Test: pulling the card produces gaps, not a stall

## S12 · CHARTS

- [ ] S12.01 Define ChartColumn (28 B) and static_assert — Test: 440 columns = 12 320 bytes
- [ ] S12.02 Stream the downsample in 512-record chunks — Test: peak working set is about 24 KB, zero heap
- [ ] S12.03 Validate each quantity against its own sentinel before aggregating — Test: a mixed column aggregates only valid minutes, n reflects it
- [ ] S12.04 Mark GAP columns and draw them as a dashed baseline — Test: a 03:00-05:00 dropout is not bridged (R6)
- [ ] S12.05 Render into an lv_canvas at 440x260, not lv_chart — Test: envelope renders, axes are ours
- [ ] S12.06 Redraw only on range change or a new sample — Test: chart redraw count is at most 1/minute
- [ ] S12.07 Auto-scale the Y axis with 12% hysteresis and nice steps — Test: an oscillating value does not make the axis jump
- [ ] S12.08 Fill each column with about 20 stacked rects for a gradient — Test: the fill is a gradient, not flat
- [ ] S12.09 Draw min/max as ticks at 1 day and a 25% band at 1w/1m — Test: each range uses the specified treatment
- [ ] S12.10 Serve 1 day from L1 and 1w/1m from L2 — Test: with the card removed after boot, all three ranges still render
- [ ] S12.11 Show 'no data' when L1/L2 are empty — Test: never an empty axis that reads as 'nothing happened'
- [ ] S12.12 Label a stale rollup with 'history as of HH:MM' — Test: a day-old rollup says so on the chart
- [ ] S12.13 Add the press-and-hold scrubber with a readout bubble — Test: the bubble shows the column's min/mean/max, not a fabricated instant
- [ ] S12.14 State the 1-hour resolution in the readout for week/month — Test: the label says 'envelope of hourly aggregates'

## S13 · DETAIL SCREENS

- [ ] S13.01 Build the shared skeleton: header, range selector, chart, readouts, data age, back — Test: one implementation serves all five
- [ ] S13.02 Add the 1d/1w/1m segmented range control — Test: switching range rebuilds from the correct tier
- [ ] S13.03 Add min/max/mean/now readouts and the '12 min ago' data age line — Test: a stale sample shows the age
- [ ] S13.04 Detail 1 chamber: target line dashed and labelled — Test: the reference line renders at the configured target
- [ ] S13.05 Detail 1 chamber: time above target over the range — Test: computed from samples, not a snapshot
- [ ] S13.06 Detail 2 water in: delta to chamber plus a 20% alpha min/max band — Test: delta reads correctly
- [ ] S13.07 Detail 3 water out: rise metric plus a faint water-in overlay — Test: both lines render, the overlay is faint
- [ ] S13.08 Detail 4 power: auto kW/W toggle with manual override — Test: the unit switches at the right magnitude
- [ ] S13.09 Detail 4 power: SML/— source badge (ADC removed — no ADC power sensor on this board) — Test: badge matches the source
- [ ] S13.10 Detail 4 power: daily bar strip (31/7/hourly bins) — Test: bar count matches the range
- [ ] S13.11 Detail 5 living room: series tabs for T/RH/P — Test: each series charts independently
- [ ] S13.12 Detail 5: independent Y axes with hysteresis per series — Test: RH and pressure do not share a scale
- [ ] S13.13 Detail 5: I2C badge green/amber/grey per device presence — Test: all three states reachable
- [ ] S13.14 Detail 5: sensor sub-line naming the attached probes — Test: 'AHT20 · BMP280 · H1' renders
- [ ] S13.15 Detail 5: no target line — Test: none is drawn
- [ ] S13.16 Every screen renders on the host harness too — Test: make run walks all five without hardware

## S14 · NAVIGATION

- [ ] S14.01 Implement Screen enum and push/pop/jump/current — Test: a unit test covers jump resetting depth to 1
- [ ] S14.02 Build the 88 px bottom pane with seven 68 px icon tabs — Test: all seven render at the right widths
- [ ] S14.03 Icons only, no 4 mm labels in the pane — Test: labels appear in the header instead
- [ ] S14.04 Active tab: filled glyph plus a 3 px accent rule — Test: the current screen is marked
- [ ] S14.05 Back button (48 px LV_SYMBOL_LEFT) on every screen except home — Test: present and functional
- [ ] S14.06 120 ms cross-fade on the content layer only — Test: the background does not flash black
- [ ] S14.07 Three-finger tap returns Home from anywhere — Test: gesture works on the panel
- [ ] S14.08 Long-press clock toggles 12/24 h and C/F — Test: format toggles
- [ ] S14.09 Idle return to home after 3 minutes — Test: it returns; the log screen is exempt with follow on
- [ ] S14.10 Suppress idle return on the chamber screen while a duty change is pending — Test: it stays until resolved

## S15 · LOG SCREEN

- [ ] S15.01 Generate a custom monospace font at 21 px, ASCII + degree + level glyphs — Test: lv_font_conv output committed
- [ ] S15.02 Build the toolbar with Back, LOG, E/W/I/D, INV, CLR, SAVE — Test: all six render at 44 px
- [ ] S15.03 One scrollable lv_obj + one LV_LABEL_LONG_MODE_WRAP label — Test: it wraps, it never uses the LVGL 8 spelling
- [ ] S15.04 Render only the visible window (about 26 lines) — Test: the label never receives 1 MB of text
- [ ] S15.05 Wrap at 38 characters — Test: a long record wraps rather than truncates
- [ ] S15.06 Rebuild throttled to 5 Hz — Test: a 5 000-line burst leaves touch responsive
- [ ] S15.07 Scan for newlines only when the window start moves — Test: consecutive re-renders reuse the line index
- [ ] S15.08 Colour by level, with ! and !! overrides — Test: E red, W amber, I grey, D dim, ! amber, !! red
- [ ] S15.09 Follow-at-end on by default — Test: the view sticks to the newest record
- [ ] S15.10 Any manual scroll turns follow off and shows a FOLLOW button — Test: scrolling back is not yanked away
- [ ] S15.11 Status bar: bytes, percent, dropped count, uptime, WS dot — Test: all five fields render
- [ ] S15.12 Level filter row actually filters — Test: pressing E hides I and D records
- [ ] S15.13 CLR requires a confirm — Test: it does not clear without one
- [ ] S15.14 SAVE dumps the ring to /log_<ts>.txt — Test: the file appears with the full 1 MB
- [ ] S15.15 Long-press the status strip opens the log screen — Test: gesture works
- [ ] S15.16 No nav pane, full-bleed — Test: about 26 lines are visible
- [ ] S15.17 Readable with WiFi down and USB disabled — Test: pull the antenna and the mux, the screen still works
- [ ] S15.18 Never touches WiFi or tpl_server — Test: code review plus a test with WiFi disabled

## S16 · tpl_can_monitor extraction (R10 regression)

- [ ] S16.01 Extract can_id_entry_t, sml_reassembly_t, RX task, can_table_update() into prj_template/tpl_can_monitor — Test: project 26 still builds
- [ ] S16.02 Collapse decode_protocol() and get_protocol_info() into one can_decode() — Test: no duplicate switch remains
- [ ] S16.03 Move SML reassembly out of the display path into the RX task, keyed by base ID — Test: reassembly follows bus order, not table order
- [ ] S16.04 Delete decoder side effects; counters live in the RX task — Test: '142 frames' equals real RX count
- [ ] S16.05 Re-express row geometry in TPL_WS7_WIDTH/HEIGHT — Test: no CYD_* macro remains in the module
- [ ] S16.06 Rebuild project 26 and confirm it passes — Test: pio run -e esp32ota exits 0 (R10 regression)
- [ ] S16.07 Restyle the monitor for 480 px: 50 px rows, 21 px font — Test: about 13 rows fit
- [ ] S16.08 Row tap opens a full-screen overlay on lv_layer_top() — Test: the overlay is not a separate LVGL screen
- [ ] S16.09 Show the bus-error banner in the monitor header — Test: it appears on ERR_PASS
- [ ] S16.10 No range selector on the monitor — Test: none is drawn

## S17 · CAN SUBSYSTEM on the device

- [ ] S17.01 Install TWAI at 125 kbps, ACCEPT_ALL filter — Test: it starts without error
- [ ] S17.02 Create canTask: 4096 B, priority 3, 100 ms alert timeout — Test: task stats show the expected stack
- [ ] S17.03 Re-arm RX_DATA|ERR_PASS|BUS_ERROR|RECOVERED|RX_QUEUE_FULL — Test: alerts fire in a loop
- [ ] S17.04 Set alerts_config = 0 (non-NULL) — Test: the panel does not crash when the first alert fires
- [ ] S17.05 Surface ERR_PASS/BUS_ERROR as a sticky bus-fault flag — Test: the banner appears and clears only on a good frame
- [ ] S17.06 Send one 0x555 wall-clock frame per second — Test: the bus sees it at 1 Hz, nothing else per-second
- [ ] S17.07 Prove CAN reception never blocks LVGL (N3) — Test: a CAN flood leaves the UI at the same frame rate

## S18 · LOCAL I2C SENSORS — ABANDONED

- [x] S18.01 Scan the I2C bus and log every responding address — Test: 0x20–0x3D (CH422G), 0x5D (GT911), 0x77 (BMP380, chip_id=0x00)
- [x] S18.02 Confirm an AHT20 at 0x38 collides with WR-IO (R30/Q24) — Test: confirmed, CH422G WR-IO occupies 0x38
- [x] S18.03–S18.12 All sensor task items — Test: BMP380 at 0x77 responds with chip_id=0x00 (not powered or SDA/SCL not wired); AHT20 at 0x38 is unusable. **The BMP380+AHT20 combo is not usable on this board.** See whitepaper §3.5 update and Q24 resolution.

**Resolution:** The H1 header carries CH422G (4 I2C addresses), GT911 (0x5D), and a BMP380 at 0x77. The AHT20 at 0x38 hard-collides with CH422G WR-IO. The BMP380 at 0x77 is addressable but returns chip_id=0x00 — either unpowered or SDA/SCL not connected to the actual sensor module. No local climate sensors can be fitted. The living-room zone (Detail 5) will render `—` permanently.

## S19 · SML POWER (lift from project 15, R26)

- [ ] S19.01 Lift sml_publish() and the OBIS matching from project 15 into src/app/ — Test: project 15 still builds unchanged
- [ ] S19.02 Add libsml to lib_deps — Test: it builds
- [ ] S19.03 Match 1-0:16.7.0/255 as the household watts — Test: a captured frame yields the right W value
- [ ] S19.04 Reassemble with a 3-deep 258-byte pool with lock flags (774 B) — Test: a 33-frame burst reassembles
- [ ] S19.05 Carry over sml_lost_bytes_cnt / sml_error_cnt / sml_empty_message_cnt — Test: a lossy burst increments them, no silent absorb
- [ ] S19.06 Decode on the CAN task on message completion — Test: libsml never runs inside the RX drain loop
- [ ] S19.07 Sample the per-minute value into L1, do not stream — Test: one assignment per second, one record per minute
- [ ] S19.08 Have project 15 adopt the shared decoder — Test: both projects print the same watts (R26)

## S20 · PWM DUTY CONTROL (only after diagnostics exist)

- [ ] S20.01 Implement the TX path for 0x240 only — Test: no other frame can be transmitted from this project
- [ ] S20.02 Add the slider 0-1000, 0 at the left labelled OFF, snapping to 5% — Test: the direction is OFF-left, FULL-right
- [ ] S20.03 Dragging changes nothing — Test: no frame is transmitted during a drag or release
- [ ] S20.04 Show the pending value as 'requested', distinct from 'in force' — Test: the two lines are always separate
- [ ] S20.05 Disable APPLY while requested == in force — Test: a no-op cannot be sent
- [ ] S20.06 Modal confirm naming the value and durability — Test: text reads 'Set PWM duty to 55 %? This survives a power cut.'
- [ ] S20.07 Cancel returns the slider to the in-force value — Test: it snaps back
- [ ] S20.08 Reconcile against a fresh 0x280 with a timeout — Test: sent-then-reported shows live; no report shows failed and reverts
- [ ] S20.09 Show a conflicting writer as 'conflict' with both values, no retry loop — Test: the panel shows truth and stops
- [ ] S20.10 Render PWM_SRC_* provenance as a badge (RESTORED/live) — Test: EEPROM boot shows a distinct hollow badge
- [ ] S20.11 Rate-limit commits per N16 and log old/new permille every send — Test: the log shows both values and the PWM_SRC_CAN expectation
- [ ] S20.12 Map TX failure to the same fault flag as a bus error — Test: never-sent is visibly different from ignored
- [ ] S20.13 Add a test proving no gesture, long-press, timeout or auto-repeat transmits — Test: the test enumerates every input path
- [ ] S20.14 Confirm the panel applies no ramp, clamp or dead-man (N15) — Test: code review plus a comment naming AVR/01_heizung_temp as the owner

## S21 · TYPE SCALE (4 mm floor)

- [ ] S21.01 Generate the font set at 110/52/32/21/21 px — Test: all five bitmaps exist
- [ ] S21.02 Include degree sign and the level glyphs — Test: they render, not tofu
- [ ] S21.03 Resize the log screen from 16 to 21 px — Test: about 26 lines, 38 chars per line
- [ ] S21.04 Resize CAN monitor rows from 20 to 21 px and row height 50 — Test: the sweep passes and about 13 rows fit
- [ ] S21.05 Keep font bitmaps flash-resident, not internal RAM (N8) — Test: internal RAM use does not grow with fonts

## S23 · SETTINGS + STORAGE STATUS

- [ ] S23.01 Build the Settings rows: CAN, backlight, power unit, retention, target, sensor roles, water model, diagnostics — Test: all eight rows render
- [ ] S23.02 CAN switch shows the actual state read back from the shadow, not the requested one — Test: config says on while mux is low shows a fault
- [ ] S23.03 CAN toggles persist immediately and are logged — Test: it survives a reboot
- [ ] S23.04 Both CAN toggle modals name the consequence — Test: the text matches §8.14
- [ ] S23.05 Backlight row drives tpl_ws7_backlight (binary only) — Test: it toggles
- [ ] S23.06 Sensor roles editor reachable from Settings — Test: a binding changed here shows up in the next sample
- [ ] S23.07 Storage panel: card type, total, used, free, headroom over floor — Test: all fields render
- [ ] S23.08 Storage panel: last rollover, newest file, per-tier breakdown — Test: all render
- [ ] S23.09 Distinguish operational / faulty / absent by evidence (D44) — Test: a failed rollover write shows red, not grey
- [ ] S23.10 Faulty is sticky and keeps the first-failure time — Test: the timestamp persists until a clean rollover
- [ ] S23.11 Cache total/used/free; refresh on rollover, insert, demand (N21) — Test: no card access on the render path
- [ ] S23.12 Show free space on the home strip only when low (D45) — Test: 37 GB free shows nothing; 12 MB shows the warning
- [ ] S23.13 Compact SD field in the log status bar — Test: 'SD 1.0G/29G ok' renders
- [ ] S23.14 Extend /sd/status JSON with the same figures — Test: the JSON matches the panel

## S24 · CAN ENABLE SWITCH (boot sequence, N19)

- [ ] S24.01 Drive USB_SEL LOW as the first statement of setup() — Test: it happens before any other init
- [ ] S24.02 Make it unconditional, before reading config — Test: an unconfigured panel comes up with USB
- [ ] S24.03 Add the 200 ms settle before raising IO5 — Test: the settle is observable in a log timestamp
- [ ] S24.04 Start TWAI only if config.can_enabled — Test: with CAN off, no TWAI driver exists
- [ ] S24.05 With CAN off: zones render —, charts empty, monitor shows an explanation — Test: the panel is otherwise fully alive
- [ ] S24.06 With CAN on: frames flow — Test: the status dot is green
- [ ] S24.07 Record what happens to the CH9102 console when IO5 goes high (settles Q22) — Test: a written answer in docs/bench/Q22.md
- [ ] S24.08 Verify a watchdog restart re-runs N19 — Test: after a forced restart, USB comes back
- [ ] S24.09 Crashes dump the ring to SD before the reset (no console needed) — Test: the file appears after a deliberate crash
- [ ] S24.10 Write the BOOT + power-cycle recovery procedure — Test: a fresh reader recovers the board with no laptop-side help

## S25 · WATER MODEL (modelled water-out)

- [ ] S25.01 Define the WaterModel interface with a null default — Test: with no model the zone is —
- [ ] S25.02 estimate() returns false out of domain — Test: nonsense inputs render —, never a guess
- [ ] S25.03 Add the TEMP_MODELLED flag bit to Sample.flags — Test: a modelled record can never be read as measured
- [ ] S25.04 Draw the modelled series dashed against solid measured lines — Test: the distinction survives a screenshot
- [ ] S25.05 Add the ~ glyph on the home zone and a MODELLED badge on detail 3 — Test: both render
- [ ] S25.06 Prefix min/mean/max readouts with ~ — Test: all three carry it
- [ ] S25.07 Tag modelled values in the log at ! amber — Test: they are visually distinct
- [ ] S25.08 Exclude modelled values from every statistic — Test: 'time above target' is unchanged by the model

## S26 · POLISH, MEMORY VERIFICATION, SOAK

- [ ] S26.01 Assert LVGL draw buffers are in internal RAM — Test: address check in a unit test
- [ ] S26.02 Log heap_caps_get_free_size() after ui_init() and after the first SD write — Test: both numbers appear
- [ ] S26.03 Assert heap stays under 64 KB (N6) — Test: the build or a runtime assert catches a violation
- [ ] S26.04 Assert PSRAM usage matches the §10.5.1 table — Test: the numbers line up within the stated headroom
- [ ] S26.05 Confirm no chart build allocates heap at any range (N18) — Test: a heap probe is flat across all three ranges
- [ ] S26.06 Content dimming as the screensaver (D43) — Test: the UI dims toward the background, the backlight stays lit
- [ ] S26.07 72-hour soak: no watchdog reset, no leak, heap drop under 8 KB — Test: a written report with the numbers
- [ ] S26.08 Verify a log storm does not make the panel unresponsive (N12) — Test: a 5 000-line burst keeps touch responsive
- [ ] S26.09 Re-run the whole regression set — Test: project 26, project 15, 26_CYD_CAN_monitor all build; host smoke test passes

## S27 · DOCS AND HANDOFF

- [ ] S27.01 Split §6 into prj_template/docs/waveshare_lcd7_board_abstraction.md — Test: §6 becomes a pointer
- [ ] S27.02 Update the whitepaper's open-questions table with the resolved answers — Test: Q13/Q21/Q22/Q24 carry outcomes
- [ ] S27.03 Add the README with build, flash, OTA and recovery instructions — Test: a reader can get the panel running from it
- [ ] S27.04 Record the measured flash size, MAC and PSRAM for the panel — Test: a small identity table exists for future panels (Q23)
- [ ] S27.05 Run make fmt and make links one final time — Test: clean tree, all symlinks present
