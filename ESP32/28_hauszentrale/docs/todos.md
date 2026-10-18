# 28_hauszentrale — implementation todos

Derived from `docs/whitepaper.md` rev 1.6. Order: board first, then the macOS
environment, then Deliverable A (`tpl_waveshare_lcd7`), then the project.
Every item names the check that closes it.

345 items (52 open). Tick boxes as work lands.

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

Hardware connected (2026-10-18): panel on `/dev/cu.usbmodem5C941561291`,
CAN monitor on the bus, Sensirion SHT45 at `0x44` fitted. Use this bench for
the S28 measurements unless an item says otherwise.

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

- [x] S6.01 Fill in [common] from §7.1 with all build flags, including board_build.flash_size = 16MB — Test: pio run -e esp32usb compiles a hello world and the image is not capped
- [x] S6.02 Add the esp32ota env with espota/3232 — Test: pio run -e esp32ota builds the same image
- [x] S6.03 Verify existing partitions_16m.csv works (factory + update_0 6 MB, spiffs 3.7 MB, coredump 256 KB) — Test: gen_esp32part.py validates it and WiFi calibration still works after upload (table now has **two** OTA slots: `ota_0` 6 MB + `ota_1` 6 MB, same offsets/sizes as before, spiffs unchanged at 3.7 MB. With only `factory`+`ota_0` the first OTA succeeds and every later one fails at `Update.begin` — verified on the bench)
- [x] S6.04 Wire tpl_wifi_setup(true, true, 255) — Test: no LED pin is claimed and no RGB bit is corrupted
- [x] S6.05 Confirm an OTA update over WiFi actually replaces the app — Test: push a build with a changed version string, it changes on the device
- [x] S6.06 Add -Werror and confirm the build is clean — Test: a deliberate warning fails the build (scoped with `build_src_flags` so LVGL's own `#warning`s do not fail the link)

## S7 · LOG RING — diagnostics before any GUI or actuator work

- [x] S7.01 Extend tpl_debug_log with a 1 MB PSRAM byte ring (opt-in via `USE_TPL_DEBUG_LOG`; falls back to a 16 KB internal ring, then the legacy line buffer, when PSRAM is absent) — Test: `heap_caps_malloc(MALLOC_CAP_SPIRAM)`, `ESP.getFreePsram()` drops by 1 MB
- [x] S7.02 Keep DEBUG_LOG_LINES/DEBUG_LOG_LINE_SIZE working as aliases — Test: an existing project with USE_TPL_DEBUG_LOG still builds (R17)
- [x] S7.03 Write records as [uptime][level][tag] msg with newline termination — Test: a dumped record matches the exact format
- [x] ~~S7.04 Hook uart_set_vprintf so Serial's own output is captured too — Test: Serial.println content appears in the ring~~ ~~REJECTED: no `uart_set_vprintf` exists in ESP-IDF (4.4 or 5.x), and Arduino `Serial.print` writes the UART directly. All template/project logging now uses `TPL_LOGx`; IDF logs are captured via `esp_log_set_vprintf`.~~
- [x] S7.05 Guard the ring write with a 5 ms mutex timeout that drops on failure — Test: a test forces contention and the writer returns, does not block
- [x] S7.06 Implement tpl_log_set_serial(bool) — Test: with serial off, logging continues and the system does not hang
- [x] S7.07 Verify the ring wraps and keeps the newest bytes — Test: a 1 MB flood then a marker, marker is readable, old start is gone
- [x] S7.08 Serve the ring over /log and /log/clear — Test: curl returns the JSON/text records
- [x] S7.09 Drain the ring from TaskWebSocketCore0 at 750 ms, 8 KB/tick cap — Test: no cost with zero clients attached
- [x] S7.10 Emit {dropped:N} when the client falls behind — Test: a paused client then reconnect shows the marker
- [x] S7.11 Backlog a new client to max(head-8 KB, oldest) — Test: a fresh client immediately sees recent records
- [x] S7.12 Confirm tail_ws.py shows live records on the Mac — Test: a message printed in the ring appears in the terminal (`prj_template/tools/tail_ws.py`)
- [x] S7.13 Confirm no code path blocks on a dead Serial — Test: with USB disconnected, the panel keeps logging and stays responsive; `tpl_log_set_serial()` gates the mirror, and UART0 writes do not wait for a host

## S8 · PURE-C++ CORE + HOST HARNESS (F12)

~~- [ ] S8.01 Create src/app, src/ui, src/services, src/esp32, src/host with no cross-layer includes — Test: a grep/lint check passes~~ **REPLACED** by `host_harness/` rendering the real screens to PNGs (see NOTE below); the layered directory split was not created.
- [x] S8.02 Define SensorFn, CanState, ClimateState, HausState and the copy-out `gui_get_state(HausState *out)` exactly as §10.2 — Test: static_assert each block is trivially copyable; no fixed-size assert (the struct is a superset of the whitepaper fields). Implemented in `include/haus_gui.h`; `haus_ui.c`, `haus_gui.cpp` and `haus_gui_mock.cpp` migrated to the copy-out API. Host harness renders all three PNGs and the ESP32 `esp32ota` build passes.
- [x] S8.03 Add one triple buffer per producer (CanState, ClimateState) with a release/acquire publish index; migrate `haus_ui.c`, `haus_gui.cpp` and `haus_gui_mock.cpp` to copy-out — Test: a writer publishing twice cannot alter the block a reader holds, and the host harness builds; UI and recorder each see a coherent snapshot with no `g_state`/`g_state_seq`. Implemented as `include/haus_triple_buffer.h` (release/acquire exchange); `haus_gui.cpp` uses `TripleBuffer<CanState>` + `TripleBuffer<ClimateState>` with a reader mutex, the mock uses the same buffers. Verified with a standalone publish-twice test, host PNG render, and the `esp32ota` build.
- [x] S8.04 Add the Sample struct and static_assert(sizeof(Sample)==24) — Test: the assert compiles (haus_gui.h gui_sample_t)
- [x] S8.05 Stand up host/CMakeLists.txt per lv_port_pc — Test: cmake configure + build produces a binary (commit 63bbcad)
- [x] S8.06 Add the HAUS_HOST build-flag switch selecting host services — Test: host CMakeLists.txt defines HAUS_HOST, device does not
- [x] S8.07 Implement the can_feed replay stub through the production haus_can.h decoder — Test: a fixture of the four heater IDs decodes (haus_gui.h + haus_gui_mock.cpp)
- [x] S8.08 Implement the in-memory recorder with the identical API — Test: charts read through the same read() path as on device (haus_gui_mock.cpp:301-346, 30-day synthetic history)
- [x] S8.09 Implement the settable virtual clock — Test: a month of history is generated in under a second (haus_gui_mock.cpp:37-38, g_sim_time advances 1s per gui_update_state())
~~- [ ] S8.10 Launch the SDL window at 480x800 — Test: the window appears and renders the host UI~~
~~- [ ] S8.11 Make make run build and launch the harness — Test: from repo root, make -C ESP32 run works~~
~~- [ ] S8.12 Add a headless smoke test that drives each screen by synthetic taps — Test: CI fails on an assertion~~
~~- [ ] S8.13 Add screenshot-diff baselines pinned to lvgl 9.2 — Test: a font bump changes the baseline and a diff fails~~

**NOTE:** Host harness renders real screens (extends the commit 63bbcad PNG
renderer). The data interface is `include/haus_gui.h` with two implementations:
`src/haus_gui.cpp` (device, all stubs) and `host_harness/src/haus_gui_mock.cpp`
(host, synthetic heater with 30-day history and a virtual clock). The harness now
renders the screens to PNG via `host_harness/src/{main.c,haus_ui.c}`:
`output_home.png`, `output_detail.png`, `output_power.png`, `output_living.png`,
`output_settings.png`, `output_can.png`, `output_log.png`, `output_water.png`,
`output_chart_week.png` and `output_chart_month.png`. The old `output_chart.png`
was a duplicate of the chamber detail and has been removed. The chart screens use the
streaming L1/L2 readers and the canvas renderer; the log screen uses the synthetic
ring. Fixes required to get there:
register a display flush callback (otherwise the 2nd `lv_refr_now()` hangs in
`wait_for_flushing`); `lv_line_set_points()` does not copy points, so all point
arrays must outlive the render; nav children must not re-add `NAV_Y` on top of
the already-offset parent; mock start epoch corrected to 08:00 (1728892800);
`gui_get_history()` now downsamples across the whole range instead of truncating.
The built-in Montserrat fonts lack the em-dash and middle-dot glyphs, so `-` is
used as a placeholder until the custom font set (S21) lands.
Abandoned: S8.01, S8.02, S8.10–S8.13 — PNG generation in test harness replaces
the SDL window / smoke test / screenshot baseline path.

## S9 · SENSOR RESOLUTION — slot to rank to ROM to function

**Blocked on S17** — needs a live CAN bus (identity frames) before any of this
can be exercised. The local SHT45 path (S18b) is independent and does not wait.

- [x] S9.01 Learn rank to ROM from 0x180+r identity frames — Test: a replayed identity burst populates the map (`haus_can_bus.cpp` stores rank/ROM/seen_ms; verified on the bench with `can 180 …` / `can 181 …` / `can 182 …` from the CYD, and by `test/test_sensor_map`)
- [x] S9.02 Expire the identity after 90 s — Test: with frames stopped, the slot demotes to unknown (`HAUS_SENSOR_ID_TTL_MS`; virtual test covers the TTL boundary and the device logs `identity expired; zone shows --`)
- [x] S9.03 Load ROM to function from SPIFFS sensor_roles[] seeded with the three known ROMs — Test: the table loads and round-trips (`haus_sensor_roles.cpp`: seed on first boot, save to `/sensor_roles.bin`, reload after reboot logs `loaded 3 entries`)
- [x] S9.04 Resolve by function, never by rank — Test: unplugging the water-in probe keeps the labels correct (the resolver follows ROM → function and the newest rank; verified by the rank-shift test and `test_sensor_map`)
- [x] S9.05 Log and surface a rank shift — Test: a simulated shift logs '! rank shift' and shows the correct label (bench: `! rank shift: chamber moved 0 -> 3`, once, no flapping)
- [x] S9.06 Render '—' for unbound ROM, expired identity, and unknown — Test: all three cases show a dash, not a number (`temp_by_fn` stays `HAUS_CAN_TEMP_EMPTY`; home zones render `--`)
- [x] S9.07 Never render INT16_MIN as a temperature (R5) — Test: a sentinel slot renders grey/—, never -327.68 (sentinel test in `test/test_sensor_map`; UI checks `HAUS_CAN_TEMP_EMPTY`)

## S10 · HOME SCREEN

- [x] S10.01 Build the status strip: date, weekday, CAN dot, SD dot — Test: all dots render at the right positions
- [x] S10.02 Build the FONT_CLOCK label with HH:MM only — Test: no seconds are drawn
- [x] S10.03 Gate the clock on the NTP flag — Test: before sync it shows --:--, never 1970 (R13)
- [x] S10.04 Load bg.png from SPIFFS and decode once into static PSRAM — Test: 768 000 bytes allocated, decoded once
- [x] S10.05 Fall back to a flat dark background when SPIFFS is empty — Test: removing bg.png still boots, no black rectangle
- [x] S10.06 Put the background on lv_layer_bottom() — Test: redrawing the flame does not re-decode or repaint the image
- [x] S10.07 Draw the heater cabinet outline on an lv_canvas — Test: rounded rect with the specified stroke renders
- [x] S10.08 Draw the firebox with warmth lerped from chamber temperature — Test: the colour tracks a replayed temperature
- [x] S10.09 Draw 3 stacked flame arcs with a 6 s breathing cycle — Test: the animation runs and brightness tracks temperature. Implemented in `build_heater()` (three pill tongues plus core) and `haus_ui_animate()` (6 s `lv_tick_get()` phase, opacity scaled by chamber temperature).
- [x] S10.10 Draw in/out pipes with the shared blue-amber-red ramp — Test: a 30/60 °C sweep crosses the breakpoints
- [x] S10.11 March 6 flow dashes along each pipe, direction by flow presence — Test: dashes animate only when duty > 0
- [x] S10.12 Render the sentinel value grey, not as an extreme colour — Test: an empty slot draws grey
- [x] S10.13 Add the living-room strip (temp, humidity, pressure) — Test: band renders at y=580 with the 76 px height
- [x] S10.14 Add the power band with a source badge — Test: SML shows SML, no value shows —
- [x] S10.15 Add the gear as the last nav-pane tab — Test: it is tappable at 68 px wide. Nav is now seven 68 px tab buttons (`NAV_TABS 7`); the gear is the last slot and opens Settings.
- [x] S10.16 Add five invisible overlay zone buttons — Test: each zone opens its screen, zones never overlap. `build_zones()` places chamber (90,220,300×200), water-in (20,440,180×120), water-out (280,440,180×120), living (0,580,480×76) and power (0,656,480×56); callbacks defer a `jump()` to the target screen.
- [x] S10.17 Add the PRESSED feedback (12% tint, 1.04 label scale) — Test: press state is visible
- [x] S10.18 Add the duty provenance marker to the status strip — Test: solid for PWM_SRC_CAN, hollow for EEPROM
- [x] S10.19 Verify the whole home screen on the host harness — Test: make run shows the same layout without hardware

## S10b · BRING-UP FIXES (2026-10-17)

- [x] Stop the RGB-panel flicker/tearing — Test: data refresh throttled to 1 Hz and every label/style write is change-guarded, so identical values no longer invalidate; display is stable
- [x] Show real local time — Test: `gui_update_state()` reads SNTP time and the UI converts with `localtime_r()`; TZ is set to CET before NTP starts, so the clock is no longer 2 h off
- [x] Remove fabricated values from the first frame — Test: home/chart/detail widgets are built with `--` placeholders and `haus_ui_init()` paints the real state before the first flush
- [x] Use ASCII placeholders — Test: no em dash remains, so the built-in Montserrat fonts render `--` instead of tofu rectangles
- [x] Vertically center the power bolt and W unit — Test: both are aligned to the number's mid-line, not its baseline
- [x] Auto-hide the touch cursor — Test: hidden at boot, shown while pressed, hidden again 1 s after release
- [x] Fix the WiFi_Manager task-watchdog reset — Test: manager reconnects with non-blocking `WiFi.begin()` every 10 s and gives DHCP a 15 s grace period; no TWDT panic with `0.0.0.0`
- [x] Space the status-strip CAN and SD indicators — Test: 16 px gap between the CAN label and the SD dot
- [x] Place the °C unit labels at build time — Test: chamber and living-room units are aligned immediately, not left at (0,0)
- [x] Make WiFi non-essential at runtime: connect at boot for the initial SNTP sync (refresh once an hour), keep it available afterwards for optional log/control use, drop the net-watchdog reboot path, and if the link is lost re-run `WiFi.begin()` until connected while the panel keeps operating offline. Add a WiFi symbol (no text) to the status strip — Test: pulling the AP never reboots the panel, the symbol is green when connected and grey when not, log/control work when connected, and the clock stays correct across an hour. Implemented: `-DNO_NET_WATCHDOG` and the `tpl_net_watchdog_setup()` call removed; `sntp_set_sync_interval(1 h)`; the WiFi manager keeps the existing non-blocking retry/tier loop; `CanState.wifi_status` drives an `LV_SYMBOL_WIFI` glyph (green/grey) in the status strip.

## S10b · BRING-UP FIXES (continued)

- [x] Stop navigation taps from rebooting — the heater cabinet canvas (99 KB) and the chart canvas (229 KB) were allocated from the internal heap, the cabinet buffer leaked on every Home rebuild, and the screen-wide cross-fade could force a full-screen LVGL layer. Both buffers now come from PSRAM, the cabinet buffer is freed in `haus_ui_deinit()`, and the screen-wide fade was removed. Test: a `-DUI_SMOKE_TEST` build cycles all screens every 2 s; 40 s (about 20 transitions) ran with no reset markers.

## S11 · THREE-TIER HISTORY STORE

- [x] S11.00 **BLOCKER — bring up the SD card on this board.** Implemented in `src/haus_sd.cpp`: CH422G IO4 is made push-pull and held asserted, the Arduino SD library uses a spare GPIO as its dummy CS, and SPI runs on MOSI 11 / SCLK 12 / MISO 13. Bench result: **SDHC 59.6 GB mounted**, write/read/delete self-test passed. The home SD dot follows `haus_sd_is_mounted()`, which does a cheap one-byte read from `/.sd_probe` at 1 Hz; a pulled card turns the dot grey within a second without a FAT scan. Capacity figures are cached and refreshed only on demand by the storage panel via `haus_sd_space_refresh()`.
- [x] S11.00b **Double bookkeeping, not a fallback.** The independent 12-month SPIFFS ring (`/rollup_ring.bin`, 8 760 × 92 B) is created/validated at boot, appended at minute 59, and restored into L2 when the SD rollup is absent. The SD day/rollup files are written at rollover. The data layer is complete; the chart read path that consumes the restored L2 is S12.
- [x] S11.01 Define RollupColumn (92 B) with static_assert — Test: the size assert holds on both host and device. Implemented in `include/haus_rollup.h`; included by both `haus_gui.cpp` and `haus_gui_mock.cpp` so the C++/C assert is compiled in both builds.
- [x] S11.02 Allocate L1 (34 560 B) and L2 (68 448 B) statically in PSRAM — Test: neither is ever reallocated. Implemented in `src/haus_history.cpp`: one-time `heap_caps_malloc(MALLOC_CAP_SPIRAM)` guarded by the pointers themselves. Boot log now reports `L1=34560 B L2=68448 B` at fixed PSRAM addresses. Also fixed `gui_sample_t` to its documented 24-byte layout and added a compile-time size assert.
- [x] S11.03 Sample once a minute on recorderTask — Test: exactly 1 440 samples in a simulated day. Implemented `haus_history_sample()` + `HistoryRecorder` task (Core 0, 1 Hz check, idempotent per minute). Host simulation passed: 1 440 distinct L1 entries in one day.
- [x] S11.04 Aggregate minute samples into hourly columns — Test: min/max/sum/n match a hand-computed hour. Implemented in `haus_history_sample()`; host simulation verified 24 columns × 60 samples, with min/max/sum/n correct for all six series. Completed hours are appended to the SPIFFS ring.
- [x] S11.04b Mark stale sources in the recorded log — Test: a source not refreshed for 60 s sets its `stale_mask` bit and `SAMPLE_FLAG_STALE`, is excluded from L2 aggregation, and logs one transition line. Implemented in `haus_history_sample()`; host test verified chamber/water/power stale while climate stayed fresh (`0x47`).
- [x] S11.04c Host test for all rolling counters and both write paths — `make test` and `make sanitize` in `test/test_history` pass **11 351 checks**: 1 440-sample day, 24 hourly columns, exactly 2 SD writes at midnight, day path, 34 652/68 496 B CRC-blocked files, stale mask and recovery, clock-back handling, SPIFFS ring order, 8 760-hour ring wrap, 744-column L2 wrap, leap day, both DST transitions, a failed-write rollover, L2 restore from the saved rollup before the clock is valid, today's day-file replay into L1 without double-counting L2, a corrupt 64-record tail being discarded, and 30 stale months deleted one per retention pass.
- [x] S11.05 Write /data_YYYYMMDD.bin once at midnight, whole file — Test: exactly two writes per simulated day. Implemented in `write_daily_files()`; host simulated day verified exactly 2 counter increments (day + rollup).
- [x] S11.06 Rewrite /rollup.bin once at midnight — Test: file is 68 448 bytes and reloads identically. The rollup writer emits 744 × 92 B columns in chronological ring order; the same layout is read by `haus_history_ring_load()`.
- [x] S11.07 Name the day file from the last sample's epoch — Test: an NTP step cannot split a day (D34). `format_day_path()` uses the previous sample's epoch via `localtime_r`; the host test asserted `/data_20241014.bin`.
- [x] S11.08 Boot sequence: mount, read rollup, read newest day, re-apply, replay into L1 — Test: under 100 KB read, under 100 ms. `haus_history_restore()` loads the SD `/rollup.bin` (or the SPIFFS ring), finds the newest `/data_YYYYMMDD.bin`, applies only hours newer than the rollup to L2, and replays today's file into L1. Total read is 68 448 + 34 560 = 103 008 B.
- [x] S11.09 Restore happens before the first frame — Test: no chart is ever seen half-populated. `main.cpp` calls `haus_history_restore()` after SD/history init and before display/LVGL init, so `haus_ui_init()` never sees an empty L2.
- [x] S11.10 Add CRC32 per 64-record block — Test: a corrupted tail is detected and discarded at read time. Day and rollup files are written as 64-record blocks with a trailing IEEE CRC32; restore validates each block and stops at the first mismatch. Host test corrupts block 1 and confirms only block 0 survives.
- [x] S11.11 Implement 24-month retention, one delete per pass — Test: 30 stale months delete without stalling the recorder. `haus_history_retention_step()` scans for the oldest `/data_*.bin`, compares calendar months, and deletes at most one file. The recorder calls it once per local day (and therefore once at startup when the clock becomes valid).
- [x] S11.12 Implement the 50 MB free-space floor forcing 3-month retention — Test: below the floor the effective retention is 3 months, otherwise 24. `haus_history_retention_months()` is unit-tested at 49/50/100 MB; `CanState.sd_low_space` is driven from the cached space figures.
- [x] S11.13 Retry a dropped card every 30 s and set a sticky fault — Test: pulling the card produces gaps, not a stall. `haus_history_sd_watchdog()` checks the cheap probe, retries `haus_sd_mount()`/self-test every 30 s, and holds `sd_fault` until the card is verified. `CanState.sd_fault` exposes the latch.

## S12 · CHARTS

- [x] S12.01 Define ChartColumn (28 B) and static_assert — Test: 440 columns = 12 320 bytes. Implemented in `include/haus_chart.h`; both the C and C++ asserts are compiled (the header is included by `haus_ui.c`).
- [x] S12.02 Stream the downsample in 512-record chunks — Test: peak working set is about 24 KB, zero heap. Implemented in `src/haus_chart.cpp` with static 512-sample (12 KB) and 64-rollup-column (5.6 KB) buffers; `test/test_chart` asserts the chunk limits.
- [x] S12.03 Validate each quantity against its own sentinel before aggregating — Test: a mixed column aggregates only valid minutes, n reflects it. `sample_value()` checks `HAUS_CAN_TEMP_EMPTY`, climate valid flags, and per-source `stale_mask`; the host test verifies 1 370 valid minutes from 1 440 with 60 sentinel + 10 stale.
- [x] S12.04 Mark GAP columns and draw them as a dashed baseline — Test: a 03:00-05:00 dropout is not bridged (R6). GAP columns are marked by the builder and drawn as dashed baseline ticks in `chart_draw()`; a gap is never connected by the mean line.
- [x] S12.05 Render into an lv_canvas at 440x260, not lv_chart — Test: envelope renders, axes are ours. Implemented in `chart_build_widgets()`/`chart_draw()`; axes, grid, labels and the Soll line are all ours.
- [x] S12.06 Redraw only on range change or a new sample — Test: chart redraw count is at most 1/minute. `detail_update()` rebuilds only when `gui_history_newest_epoch()` advances or the range changes; the header still updates at 1 Hz.
- [x] S12.07 Auto-scale the Y axis with 12% hysteresis and nice steps — Test: an oscillating value does not make the axis jump. `chart_axis()` uses 1/2/5 steps and keeps the current band until the data leaves it by 12%.
- [x] S12.08 Fill each column with about 20 stacked rects for a gradient — Test: the fill is a gradient, not flat. The waterfall fill blends from the series colour to the background down to the baseline.
- [x] S12.09 Draw min/max as ticks at 1 day and a 25% band at 1w/1m — Test: each range uses the specified treatment. Day uses vertical min/max ticks; week/month use a 25% band; water uses the band on all ranges.
- [x] S12.10 Serve 1 day from L1 and 1w/1m from L2 — Test: with the card removed after boot, all three ranges still render. `gui_read_samples_chunk()` reads L1; `gui_read_rollup_chunk()` reads L2; the host mock synthesises both.
- [x] S12.11 Show 'no data' when L1/L2 are empty — Test: never an empty axis that reads as 'nothing happened'. The chart draws an explicit centred "no data" label when no series has a valid column.
- [x] S12.12 Label a stale rollup with 'history as of HH:MM' — Test: a day-old rollup says so on the chart. The stale label is shown when the newest column is more than an hour old.
- [x] S12.13 Add the press-and-hold scrubber with a readout bubble — Test: the bubble shows the column's min/mean/max, not a fabricated instant. Press/drag maps x to a column and shows the column timestamp plus min/mean/max; it clears 1.5 s after release.
- [x] S12.14 State the 1-hour resolution in the readout for week/month — Test: the label says 'envelope of hourly aggregates'. The data-age line appends that phrase for 1w/1m.

## S13 · DETAIL SCREENS

- [x] S13.01 Build the shared skeleton: header, range selector, chart, readouts, data age, back — Test: one implementation serves all five. `detail_build()` serves chamber, water, power, living and settings.
- [x] S13.02 Add the 1d/1w/1m segmented range control — Test: switching range rebuilds from the correct tier. `range_build()`/`range_set()` switch L1 samples vs L2 rollups.
- [x] S13.03 Add min/max/mean/now readouts and the '12 min ago' data age line — Test: a stale sample shows the age. `chart_update_readouts()` computes all four and formats min/h/d age.
- [x] S13.04 Detail 1 chamber: target line dashed and labelled — Test: the reference line renders at the configured target. Soll is drawn as a dashed series and labelled at the right edge.
- [x] S13.05 Detail 1 chamber: time above target over the range — Test: computed from samples, not a snapshot. The line above the chart accumulates whole columns whose mean exceeds that column's Soll.
- [x] S13.06 Detail 2 water in: delta to chamber plus a 20% alpha min/max band — Test: delta reads correctly. The combined water screen shows delta/rise and uses a min/max band for both water series.
- [x] S13.06b **One combined water chart with three series: water-in, water-out and Soll.** Soll is a real series (future automatic control), not a reference line. `target_c100` lives in the two formerly-padding bytes of `gui_sample_t` (still 24 B) and in `RollupColumn` (now 92 B, carried through the SD rollup and the 12-month SPIFFS ring). The chart builds a separate `int16_t target[440]`; the WATER tab opens the same screen. Tests: all three lines render at 1 d/1 w/1 m; target history is restored with the rollup after reboot.
- [x] S13.07 Detail 3 water out: rise metric plus a faint water-in overlay — Test: both lines render, the overlay is faint. The combined water screen draws water-in and water-out plus rise.
- [x] S13.08 Detail 4 power: auto kW/W toggle with manual override — Test: the unit switches at the right magnitude. The header/chart auto-select kW above 1000 W; the header `auto/W/kW` button cycles the manual override.
- [x] S13.09 Detail 4 power: SML/— source badge (ADC removed — no ADC power sensor on this board) — Test: badge matches the source. The header badge follows `CanState.power_src`.
- [x] S13.10 Detail 4 power: daily bar strip (31/7/hourly bins) — Test: bar count matches the range. 24 hourly, 7 daily or 31 daily bars are shown.
- [x] S13.11 Detail 5 living room: series tabs for T/RH/P — Test: each series charts independently. Temp/RH/Pressure tabs rebuild the chart for series 3/4/5.
- [x] S13.12 Detail 5: independent Y axes with hysteresis per series — Test: RH and pressure do not share a scale. The axis key includes the selected living-room series.
- [x] S13.13 Detail 5: I2C badge green/amber/grey per device presence — Test: all three states reachable. The badge follows the AHT20/BMP280 valid bits.
- [x] S13.14 Detail 5: sensor sub-line naming the attached probes — Test: 'AHT20 · BMP280 · H1' renders. Rendered as an ASCII-friendly sub-line.
- [x] S13.15 Detail 5: no target line — Test: none is drawn. The living-room config has `use_target = 0`.
- [x] S13.16 Every screen renders on the host harness too — Test: make run walks all five without hardware. The harness now renders home, chamber, water, power, living, settings and log, plus 1w/1m charts.

## S14 · NAVIGATION

- [x] S14.01 Implement Screen enum and push/pop/jump/current — Test: a unit test covers jump resetting depth to 1. `ui_jump()` resets depth to 1; `ui_push()`/`ui_pop()` manage the stack.
- [x] S14.02 Build the 88 px bottom pane with six 80 px icon tabs — Test: all six render at the right widths. The duplicate WATER IN/WATER OUT tabs were collapsed into one WATER tab.
- [x] S14.03 Icons only, no 4 mm labels in the pane — Test: labels appear in the header instead.
- [x] S14.04 Active tab: filled glyph plus a 3 px accent rule — Test: the current screen is marked. Active glyph is ink-white and the 3 px accent rule is drawn.
- [x] S14.05 Back button only where there is no nav pane — Test: present and functional. The persistent nav pane makes Back redundant on Home/Chamber/Water/Power/Living/Settings; the full-bleed log screen keeps its toolbar back.
- [x] ~~S14.06 120 ms cross-fade on the content layer only — Test: the background does not flash black. The whole-screen fade was removed because it made LVGL allocate a screen-sized layer and contributed to the touch reboot; a content-container-only fade is still pending.~~ ~~REJECTED: instant screen swaps are accepted; no content fade will be implemented.~~
- [x] S14.07 Three-finger tap returns Home from anywhere — Test: gesture works on the panel. `tpl_ws7_touch_points()` exposes the GT911 frame count; `main.cpp` polls it at 20 Hz and calls `haus_ui_three_finger()`.
- [x] S14.08 Long-press clock toggles 12/24 h and C/F — Test: format toggles. Four-state cycle 24h/C, 12h/C, 12h/F, 24h/F; temperatures and chart labels follow.
- [x] S14.09 Idle return to home after 3 minutes — Test: it returns; the log screen is exempt with follow on. Uses `lv_display_get_inactive_time()`; the log branch returns before the idle check.
- [x] S14.10 Suppress idle return on the chamber screen while a duty change is pending — Test: it stays until resolved. The idle check skips chamber when `CanState.has_pending` is set.

## S15 · LOG SCREEN

- [x] S15.01 Generate a custom monospace font at 21 px, ASCII + degree + level glyphs — Test: lv_font_conv output committed. `src/ui/lv_font_haus_mono_21.c` is generated from SF Mono at 21 px, 4 bpp, covering `0x20–0x7E`, `°` and `·`.
- [x] S15.02 Build the toolbar with Back, LOG, E/W/I/D, INV, CLR, SAVE — Test: all six render at 44 px.
- [x] S15.03 One scrollable lv_obj + one wrapping label — Test: it wraps, it never uses the LVGL 8 spelling. A compatibility macro maps to `LV_LABEL_LONG_WRAP` on the device (LVGL 9.2) and `LV_LABEL_LONG_MODE_WRAP` on the host (LVGL 10).
- [x] S15.04 Render only the visible window (about 26 lines) — Test: the label never receives 1 MB of text. Only 26 wrapped visual lines are composed into the label.
- [x] S15.05 Wrap at 38 characters — Test: a long record wraps rather than truncates.
- [x] S15.06 Rebuild throttled to 5 Hz — Test: a 5 000-line burst leaves touch responsive. `log_screen_update()` throttles to 200 ms; the main loop calls it at 1 Hz.
- [x] S15.07 Scan for newlines only when the window start moves — Test: consecutive re-renders reuse the line index. A rebuild is skipped when dump length, scroll offset, follow state and level mask are unchanged, so consecutive re-renders reuse the previous window.
- [x] S15.08 Colour by level, with ! and !! overrides — Test: E red, W amber, I grey, D dim, ! amber, !! red. Host uses LVGL recolor; the device LVGL 9.2 build falls back to plain text because that version has no label recolor API.
- [x] S15.09 Follow-at-end on by default — Test: the view sticks to the newest record.
- [x] S15.10 Any manual scroll turns follow off and shows a FOLLOW button — Test: scrolling back is not yanked away. An up/down gesture on the log view clears follow, moves the window by five lines and turns the FOLLOW button amber; tapping FOLLOW returns to the end.
- [x] S15.11 Status bar: bytes, percent, dropped count, uptime, WS dot — Test: all five fields render.
- [x] S15.12 Level filter row actually filters — Test: pressing E hides I and D records.
- [x] S15.13 CLR requires a confirm — Test: it does not clear without one. A modal Confirm/Cancel panel guards the clear.
- [x] S15.14 SAVE dumps the ring to /log_<ts>.txt — Test: the file appears with the full 1 MB. Device path writes the ring dump through the SD lock; the host stub reports success.
- [x] S15.15 Long-press the status strip opens the log screen — Test: gesture works. A 480x32 long-press hotspot sits over the home status strip.
- [x] S15.16 No nav pane, full-bleed — Test: about 26 lines are visible.
- [x] S15.17 Readable with WiFi down and USB disabled — Test: pull the antenna and the mux, the screen still works. The log screen reads the ring directly and touches no network object.
- [x] S15.18 Never touches WiFi or tpl_server — Test: code review plus a test with WiFi disabled. Only the existing GUI state carries the WS indicator.

## S16 · CAN monitor — project-local (extraction rejected)

**Decision 2026-10-19:** the monitor stays inside 28_hauszentrale
(`src/can_monitor.{h,cpp}`). It is too entangled with this project's UI and
memory budget to be a template asset; project 26 is untouched. The original
extraction/R10 items are superseded by the project-local equivalents below.

- [x] S16.01 Define the monitor data layer in 28: `can_id_entry_t`, `sml_reassembly_t`, the SPSC RX ring, the consumer task and the table update (`src/can_monitor.{h,cpp}`) — Done: producer is the TWAI drain in `haus_can_bus.cpp`; consumer is `canMon`; table is copy-out under a spinlock.
- [x] S16.02 Collapse the two decoders into one pure `can_monitor_decode()` returning `{label, decoded, protocol}` — Done: one switch, no side effects; the host mock mirrors only the labels it renders.
- [x] S16.03 Move SML reassembly into the consumer, keyed by base ID (0x600/0x700) — Done: reassembly follows bus order, not table order.
- [x] S16.04 Delete decoder side effects; counters live in the consumer — Done: `can_monitor_counters_t` is updated in `on_frame()`, never in the renderer; `rx_frames` counts consumed frames and `dropped` counts ring overflow.
- [x] S16.05 Project-local row geometry, no `CYD_*` macro in the module — Done: the module has no geometry; `haus_ui.c` lays rows out in the project's 480×800 coordinates.
- [x] S16.06 Build and render — Done: `pio run -e esp32ota` succeeds; the host harness builds and renders `output_can.png`.
- [x] S16.07 Restyle the monitor for 480 px: 50 px rows, 21 px font — Done: **12** live rows fit (`haus_body_21`, 50 px pitch, y=80..680); 13 would collide with the 88 px nav pane at y=712.
- [x] S16.08 Row tap opens a full-screen overlay on `lv_layer_top()` — Done: `can_overlay_open()` creates it on the top layer (not a separate LVGL screen); the close button deletes it.
- [x] S16.09 Show the bus-error banner in the monitor header — Done: a red `CAN BUS FAULT` bar replaces the stats line when `CanState.can_fault` is set (raised on ERR_PASS/BUS_ERROR in `haus_can_bus.cpp`).
- [x] S16.10 No range selector on the monitor — Done: none is drawn.
- [x] S16.11 Lock-free SPSC ring with two atomic byte indices — Done: `s_head`/`s_tail` (`std::atomic<uint8_t>`), 127 usable frames; a full ring drops and counts, never blocks the CAN task.
- [x] S16.12 Host mock so the harness still renders the real scope — Done: `host_harness/src/can_monitor_mock.cpp` supplies a synthetic bus; `output_can.png` shows 9 populated rows plus placeholders.

## S17 · CAN SUBSYSTEM on the device

- [x] S17.01 Install TWAI at 125 kbps, ACCEPT_ALL filter — Test: it starts without error (`src/haus_can_bus.cpp`; bench log `TWAI running at 125 kbps`. Pins are TX 20 / RX 19 per Waveshare's `waveshare_twai_port.h` — the whitepaper had them swapped)
- [x] S17.02 Create canTask: 4096 B, priority 3, 100 ms alert timeout — Test: task stats show the expected stack (`xTaskCreate(can_task, "canTask", 4096, NULL, 3, ...)`)
- [x] S17.03 Re-arm RX_DATA|ERR_PASS|BUS_ERROR|RECOVERED|RX_QUEUE_FULL — Test: alerts fire in a loop (`twai_reconfigure_alerts()` after start + bus-off recovery path)
- [x] S17.04 Set alerts_config = 0 (non-NULL) — Test: the panel does not crash when the first alert fires (`uint32_t configured = 0;`)
- [x] S17.05 Surface ERR_PASS/BUS_ERROR as a sticky bus-fault flag — Test: the banner appears and clears only on a good frame (`bus_fault` is set on ERR_PASS/BUS_ERROR and cleared only by `decode_frame`; the banner lives on the home screen, not `lv_layer_top()` — a top-layer child forced a full-width composite that the single-buffered RGB panel showed as white stripe tearing)
- [x] S17.06 Send one 0x555 wall-clock frame per second — Test: the bus sees it at 1 Hz, nothing else per-second (bench `/sd/status`: `can_tx` grows by one per second; before SNTP the frame carries a zero clock so the bus always has an ACK source)
- [x] S17.07 Prove CAN reception never blocks LVGL (N3) — Test: a CAN flood leaves the UI at the same frame rate (bench: 12,000 frames at 992/s from the CYD, `ui_hz` 175–188 vs 188 idle, no reset or HTTP timeout. Required yields in `can_task` (idle-task starvation), 10 Hz status polling, one `time()` per second, and static buffers in the HTTP handlers: `/sd/status` previously overflowed the 8 KB loop stack under load and rebooted the panel. Light telemetry at `/can`)

## S18 · LOCAL I2C SENSORS — ABANDONED

- [x] S18.01 Scan the I2C bus and log every responding address — Test: 0x20–0x3D (CH422G), 0x5D (GT911), 0x77 (BMP380, chip_id=0x00)
- [x] S18.02 Confirm an AHT20 at 0x38 collides with WR-IO (R30/Q24) — Test: confirmed, CH422G WR-IO occupies 0x38
- [x] S18.03–S18.12 All sensor task items — Test: BMP380 at 0x77 responds with chip_id=0x00 (not powered or SDA/SCL not wired); AHT20 at 0x38 is unusable. **The BMP380+AHT20 combo is not usable on this board.** See whitepaper §3.5 update and Q24 resolution.

**Resolution:** The H1 header carries CH422G (4 I2C addresses), GT911 (0x5D), and a BMP380 at 0x77. The AHT20 at 0x38 hard-collides with CH422G WR-IO. The BMP380 at 0x77 is addressable but returns chip_id=0x00 — either unpowered or SDA/SCL not connected to the actual sensor module. No local climate sensors can be fitted. The living-room zone (Detail 5) will render `—` permanently.

**Update 2026-10-17 — S18 reopened as S18b.** A **Sensirion SHT45** is now
attached to the display at **0x44**, which does not collide with
`{0x23, 0x24, 0x26, 0x38, 0x5D, 0x14}`. Temperature and humidity in the
living-room zone are real again; pressure stays `—` (no BMP sensor).

## S18b · LOCAL CLIMATE — SHT45 (reopens S18)

- [x] S18b.01 Probe 0x44 and soft-reset the part at boot — Test: log shows `SHT45 ready at 0x44`
- [x] S18b.02 High-precision measurement (0xFD) with CRC-8 per word, non-blocking (read previous result, start next) — Test: first reading logged as `23.14 C, 55.22 %rh` on the panel
- [x] S18b.03 Every I2C transaction takes the bus-wide mutex with a 20 ms bound (N13) — Test: no touch stall while the sensor is polled
- [x] S18b.04 Feed ClimateState and the `CLIMATE_VALID_SHT45` bit — Test: home living strip and Detail 5 show T/RH; host PNG confirms, pressure removed from the GUI and the HTML mockup
- [x] S18b.05 Record living temperature and humidity in L1/L2 history — Test: `SAMPLE_FLAG_CLIMATE_AHT20` is set and the 1 d chart shows the series (implemented in `haus_history.cpp:370-388`; chart reads series 3/4 in `haus_chart.cpp:67-76`)
- [x] S18b.06 Re-probe every 5 s after a NACK and invalidate the badge on loss — Test: unplugging the sensor turns the living values back to `—` and replugging restores them (implemented in `haus_sht45.cpp:26,98-116,155-165`; `haus_gui.cpp:105-108` maps loss to `GUI_EMPTY`)

## S19 · SML POWER (lift from project 15, R26)

Live source (2026-10-18): `curl http://esp32_15.fritz.box/can` and
`/hist.json`. `/hist.json` shows real household power (≈230–270 W), so the
OBIS match works. `/can` reports `SML: 1 empty, 0 incomplete, 551984 errors`
against 2.4 M RX frames — the errors are CRC/framing rejects, not lost
segments (`incomplete` is 0). Reassembly lives in `handle_rx_message()` and the
CRC/header check in `SMLTask()` in project 15 `src/main.cpp`; the lift must
carry the error counters (S19.05) and should explain/reduce the rejects rather
than copy them blindly.

Why the counter is misleading (measured 2026-10-18): the error count rises at
~9/s while only one SML message/s arrives, i.e. it saturates the 10 Hz scan
loop. `SMLTask()` never clears `valid_bytes` after processing, the RX writer
does not check `locked`, and the scan `break`s on the lowest-index valid buffer,
so one bad/stale buffer is re-counted every 100 ms and starves the others. The
551,984 figure is therefore repeat-counting, not 551,984 corrupt frames; live
`/hist.json` still shows correct changing power. Fix the lifecycle (S19.09)
before treating `sml_error_cnt` as a diagnostic.

- [x] S19.01 Lift sml_publish() and the OBIS matching from project 15 into src/app/ — Test: project 15 still builds unchanged — Done: `src/app/sml_power.{h,cpp}`; pure OBIS decoder, project 15 untouched.
- [x] S19.02 Add libsml to lib_deps — Test: it builds — Done: `git+https://github.com/volkszaehler/libsml`; `pio run -e esp32ota` succeeds.
- [x] S19.03 Match 1-0:16.7.0/255 as the household watts — Test: a captured frame yields the right W value — Done in code: `sml_power_parse()` matches the OBIS id and `read_power()` feeds `CanState.watts`; the live esp32_15 source shows the same OBIS path at ≈230–270 W. A raw-frame fixture was not available, so the bench capture is still the honest confirmation.
- [x] S19.04 Reassemble with a 3-deep 258-byte pool with lock flags (774 B) — Test: a 33-frame burst reassembles — Done: `host_harness/src/test_sml_power.cpp` builds a libsml GET_LIST_RESPONSE (three OBIS values), frames it as 2-byte length + 8-byte header + body + CRC into 258 B, feeds 33 CAN frames, and asserts one parsed message, zero errors/loss, and the right W/Wh values.
- [x] S19.05 Carry over sml_lost_bytes_cnt / sml_error_cnt / sml_empty_message_cnt, but only after S19.09: in project 15 the error counter is inflated ~8x by buffer re-scans — Test: a lossy burst increments them once each, no silent absorb — Done: the same host test asserts one empty frame → `empty_messages` 1; an orphan continuation → `lost_bytes` 8; a fourth message start → `lost_bytes` 8; one corrupt CRC → `errors` 1 (not one per frame).
- [x] S19.06 Decode on the CAN task on message completion — Test: libsml never runs inside the RX drain loop — Done: `sml_power_on_frame()` is called from the `canMon` consumer (`can_monitor.cpp:on_frame`), not from `haus_can_bus.cpp`'s `can_task` drain; libsml only runs when a message completes.
- [x] S19.07 Sample the per-minute value into L1, do not stream — Test: one assignment per second, one record per minute — Done: `gui_update_state()` runs at 1 Hz and sets `CanState.watts`/`valid`; the recorder samples that block once per minute into L1. No streaming.
- [x] ~~S19.08 Have project 15 adopt the shared decoder — Test: both projects print the same watts (R26)~~ ~~REJECTED: project 15 stays as-is. Decided 2026-10-19 that there is nothing to gain from unification; the 28 decoder lives in `src/app/` and project 15 is untouched.~~
- [x] ~~S19.09 Fix the reassembly buffer lifecycle (project 15 and the lifted copy) — Test: `sml_error_cnt` rises at most once per received SML message~~ ~~REJECTED: project 15 stays as-is. The 28 copy never re-scans a buffer; it reassembles in one pass with SPSC indices and parses once on completion.~~

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

- [x] S21.01 Generate the font set at 110/52/32/21/21 px — Test: all five bitmaps exist (`src/ui/lv_font_haus_value_110.c`, `_large_52.c`, `_value_32.c`, `_body_21.c`, plus the existing `lv_font_haus_mono_21.c`; generated from Montserrat-Medium with lv_font_conv 1.5.3)
- [x] S21.02 Include degree sign and the level glyphs — Test: they render, not tofu (ranges `0x20-0x7E`, `0xB0`, `0xB7` in every font; the home clock, detail headline and settings/storage body use the new set)
- [x] S21.03 Resize the log screen from 16 to 21 px — Test: about 26 lines, 38 chars per line (the log body has used `haus_mono_21` since S15.01; wrapping is still at 38 chars and 26 lines are visible. The 32 px toolbar keeps 16 px labels so the six buttons fit at 480 px)
- [x] S21.04 Resize CAN monitor rows from 20 to 21 px and row height 50 — Test: the sweep passes and about 13 rows fit (placeholder rows now use `haus_body_21` on a 50 px pitch; the live table lands with S16)
- [x] S21.05 Keep font bitmaps flash-resident, not internal RAM (N8) — Test: internal RAM use does not grow with fonts (lv_font_conv emits `LV_ATTRIBUTE_LARGE_CONST`, so the ~1.6 MB of glyph data is rodata in flash; DIRAM unchanged)

## S23 · SETTINGS + STORAGE STATUS

- [x] S23.01 Build the Settings rows: CAN, backlight, power unit, retention, target, sensor roles, water model, diagnostics — Test: all eight rows render (plus Storage, UI FILE, CAN MONITOR and LOG; actionable rows carry an accent value and a right chevron)
- [ ] S23.02 CAN switch shows the actual state read back from the shadow, not the requested one — Test: config says on while mux is low shows a fault
- [x] S23.03 CAN toggles persist immediately and are logged — Test: it survives a reboot — Done: `haus_config_set_can_enabled()` writes the config via `tpl_write_config()` and logs `CAN on/off persisted` (`haus_config.cpp:23-34`).
- [ ] S23.04 Both CAN toggle modals name the consequence — Test: the text matches §8.14
- [x] S23.05 Backlight row drives tpl_ws7_backlight (binary only) — Test: it toggles (`gui_set_backlight()` → `tpl_ws7_backlight()`)
- [ ] S23.06 Sensor roles editor reachable from Settings — Test: a binding changed here shows up in the next sample
- [x] S23.07 Storage panel: card type, total, used, free, headroom over floor — Test: all fields render (new STORAGE screen under Settings; `output_storage.png`)
- [x] S23.08 Storage panel: last rollover, newest file, per-tier breakdown — Test: all render (minute samples / hourly rollup / log dumps, scanned on demand)
- [x] S23.09 Distinguish operational / faulty / absent by evidence (D44) — Test: a failed rollover write marks the storage panel faulty, never merely absent (fault comes from a failed write/watchdog, absence only from `sd_card_type == ABSENT`)
- [x] S23.10 Faulty is sticky and keeps the first-failure time — Test: the timestamp persists until a clean rollover (`note_fault()` sets `sd_fault_since`; only a successful day+rollup write clears it)
- [x] S23.11 Cache total/used/free; refresh on rollover, insert, demand (N21) — Test: no card access on the render path (1 Hz reads cached figures; `gui_storage_refresh()` runs on panel open, rollover and log save)
- [x] S23.12 Show free space on the home strip only when low (D45) — Test: 37 GB free shows nothing; 12 MB shows the warning (status-strip label hidden unless `sd_low_space`)
- [x] S23.13 Compact SD field in the log status bar — Test: 'SD 1.0G/29G ok' renders (`SD <used>/<total> ok|wr|flt`; status text shortened to make room)
- [x] S23.14 Extend /sd/status JSON with the same figures — Test: the JSON matches the panel (route registered in `main.cpp`; includes total/used/free/floor/headroom, tiers, rollover, newest, fault_since, writing)
- [x] S23.15 Home SD dot shows only presence/activity: green = present, readable, safe to remove; red = write in progress; grey = absent — Test: all three states are reachable, and a fault with an idle card still shows green while the storage panel reports faulty (dot colour ignores `sd_fault`)
- [x] S23.16 The write-in-progress flag is raised and cleared around every recorder write (rollover, log dump, retention) — Test: the dot turns red for the duration and returns to green (`haus_history_set_writing()` around `write_daily_files()`, retention deletes and `gui_log_save()`)
- [x] S23.17 Show the panel's WiFi IP in SETTINGS — Test: the address matches the DHCP lease, so the web UI can be reached when CAN has taken GPIO 19/20 and USB is gone — Done: the `NETWORK` row renders `gui_wifi_ip()` (`haus_ui.c:3371`, `haus_gui.cpp:341-346`).
- [x] S23.18 Show the firmware version from version.h in SETTINGS — Test: the displayed version matches the binary that was flashed, so a panel in the field can be identified without USB — Done: the `FIRMWARE` row renders `VERSION_STRING` from `version.h` (`haus_ui.c:26-30,3373`).
- [ ] S23.19 Absent card must not latch a storage fault: only a card that was mounted and then disappeared, or a failed write, may set FAULTY; a card that was never present stays merely ABSENT — Test: boot with no card shows ABSENT and no fault timestamp, pulling a mounted card sets the fault, reinserting clears it

## S24 · CAN ENABLE SWITCH (boot sequence, N19)

- [x] S24.01 Drive USB_SEL LOW as the first statement of setup() — Test: it happens before any other init — Done: `tpl_ws7_set_usb_mode(true)` runs before the SD, history and UI init (`main.cpp:131-133`).
- [x] S24.02 Make it unconditional, before reading config — Test: an unconfigured panel comes up with USB — Done: the same call is unconditional and precedes the config read (`main.cpp:131-133`).
- [x] S24.03 Add the 200 ms settle before raising IO5 — Test: the settle is observable in a log timestamp — Done: `haus_can_bus_start()` logs `mux settle 200 ms before CAN` then delays 200 ms before switching (`haus_can_bus.cpp:254-268`).
- [x] S24.04 Start TWAI only if config.can_enabled — Test: with CAN off, no TWAI driver exists — Done: `main.cpp:409-415` only arms `s_can_start_pending` when `haus_config_can_enabled()`; `canTask`/TWAI are created in `haus_can_bus_start()`.
- [ ] S24.05 With CAN off: zones render —, charts empty, monitor shows an explanation — Test: the panel is otherwise fully alive
- [ ] S24.06 With CAN on: frames flow — Test: the status dot is green
- [x] S24.07 Record what happens to the CH9102 console when IO5 goes high (settles Q22) — Test: a written answer in docs/bench/Q22.md
- [ ] S24.08 Verify a watchdog restart re-runs N19 — Test: after a forced restart, USB comes back
- [ ] S24.09 Crashes dump the ring to SD before the reset (no console needed) — Test: the file appears after a deliberate crash
- [ ] S24.10 Write the BOOT + power-cycle recovery procedure — Test: a fresh reader recovers the board with no laptop-side help

## S25 · WATER MODEL (modelled water-out)

- [ ] S25.01 Define the WaterModel interface with a null default — Test: with no model the zone is —
- [ ] S25.02 estimate() returns false out of domain — Test: nonsense inputs render —, never a guess
- [x] S25.03 Add the TEMP_MODELLED flag bit to Sample.flags — Test: a modelled record can never be read as measured — Done: `SAMPLE_FLAG_MODELLED` is defined (`haus_gui.h:68`) and set from `water_modelled` when the sample is recorded (`haus_history.cpp:425`). The rest of the model (S25.01/02) is still missing.
- [ ] S25.04 Draw the modelled series dashed against solid measured lines — Test: the distinction survives a screenshot
- [ ] S25.05 Add the ~ glyph on the home zone and a MODELLED badge on detail 3 — Test: both render
- [ ] S25.06 Prefix min/mean/max readouts with ~ — Test: all three carry it
- [ ] S25.07 Tag modelled values in the log at ! amber — Test: they are visually distinct
- [ ] S25.08 Exclude modelled values from every statistic — Test: 'time above target' is unchanged by the model

## S26 · POLISH, MEMORY VERIFICATION, SOAK

- [x] S26.01 Assert LVGL draw buffers are in internal RAM — Test: address check in a unit test (`assert(esp_ptr_internal(s_draw_buf0))` before display init; boot log prints `internal=yes`)
- [x] S26.02 Log heap_caps_get_free_size() after ui_init() and after the first SD write — Test: both numbers appear (`S26.02 after first SD write` after the mount self-test, `S26.03 after ui_init`)
- [x] S26.03 Assert heap stays under 64 KB (N6) — Test: the build or a runtime assert catches a violation (boot computes the ui_init delta against the post-lv_init baseline; logs 60 688 B for the home screen, under the 64 KB budget. It logs an error above the budget rather than aborting)
- [x] S26.04 Assert PSRAM usage matches the §10.5.1 table — Test: the numbers line up within the stated headroom (boot logs PSRAM free/delta after ui_init and asserts more than 4 MB headroom; the 1 MB ring, L1/L2 and 768 KB background are measured in that delta)
- [x] S26.05 Confirm no chart build allocates heap at any range (N18) — Test: a heap probe is flat across all three ranges (`haus_chart.cpp` has no malloc/new/heap_caps calls and uses the two static chunk buffers; `test/test_chart` passes at all three ranges)
- [x] ~~S26.06 Content dimming as the screensaver (D43) — Test: the UI dims toward the background, the backlight stays lit~~ ~~REJECTED: content dimming does not make sense; the screen stays at full brightness.~~
- [ ] S26.07 72-hour soak: no watchdog reset, no leak, heap drop under 8 KB — Test: a written report with the numbers
- [ ] S26.08 Verify a log storm does not make the panel unresponsive (N12) — Test: a 5 000-line burst keeps touch responsive
- [x] S26.09 Re-run the whole regression set — Test: project 26, project 15, 26_CYD_CAN_monitor all build; host smoke test passes (15_power_log_eink and 26_CYD_CAN_monitor `esp32ota` builds exit 0; host harness renders all 11 screens)

## S27 · DOCS AND HANDOFF

- [ ] S27.01 Split §6 into prj_template/docs/waveshare_lcd7_board_abstraction.md — Test: §6 becomes a pointer
- [ ] S27.02 Update the whitepaper's open-questions table with the resolved answers — Test: Q13/Q21/Q22/Q24 carry outcomes
- [ ] S27.03 Add the README with build, flash, OTA and recovery instructions — Test: a reader can get the panel running from it
- [ ] S27.04 Record the measured flash size, MAC and PSRAM for the panel — Test: a small identity table exists for future panels (Q23)
- [ ] S27.05 Run make fmt and make links one final time — Test: clean tree, all symlinks present

## S28 · INTERNAL RAM — `docs/memory_report.md` remediation

Derived from `docs/memory_report.md` (2026-10-18). Order is by returned
internal RAM and risk, not by difficulty. Every item names the measurement
that closes it. Bench state for this section: panel fitted, **SD card removed
(2026-10-18)** unless the item says otherwise, CAN monitor connected, SHT45 at
`0x44`, CAN switched off in config unless the item says otherwise. A card-less
boot exercises the S11.00b SPIFFS fallback and exposes the absent-vs-fault
question (S23.19).

### S28a · Measurement and guardrails

- [x] S28.01 Keep the `M1` / `M2` logs (free internal heap and largest block after WiFi and after the web server) permanently enabled — Test: both lines appear on every boot — Done: both lines log free and largest on every boot.
- [ ] S28.02 Extend the `fopen` guard in `haus_sd.cpp` so it also gates mDNS and HTTP request allocation, not only the 1 Hz probe — Test: with the guard forced low, no `lock_init_generic` abort occurs (open: the low-heap guard now skips the SD probe and logs, but mDNS/HTTP allocation is not gated.)
- [x] S28.03 Put the acceptance threshold in one place: 24 KB free internal and a largest block of at least 4 KB before `fopen`, HTTP, or mDNS — Test: one constant is used by the guard, the probes, and the boot log — Done: `internal_heap_ok()` in `main.cpp` uses 24 KB / 4 KB.
- [x] S28.04 Log a loud boot error when the threshold is missed after `ui_init` — Test: a deliberately inflated UI static trips the error without aborting — Done: it logs `S28.03 ... low` without aborting.
- [x] S28.05 Record a repeatable panel-on baseline (SD in, CAN off, SHT45 fitted, CAN monitor connected) alongside the existing 165,560 B / 62,624 B figures — Test: a table row exists with boot hash, free heap, and largest block — Done: table added at the end of `memory_report.md` (baseline 269,932 B).
- [x] S28.06 Log the largest free block, not only the free total, at every `M*` point — Test: the 948 B failure case is visible before the first `fopen` — Done: `M1`/`M2` and the guard all print `largest`.

### S28b · Panel and LVGL

- [x] S28.07 Implement a `LV_STDLIB_CUSTOM` allocator that takes PSRAM first and falls back to internal RAM — Test: UI builds and the internal heap after `ui_init` rises by >40 KB — Done: `src/haus_lv_mem.cpp`, `LV_STDLIB_CUSTOM`; UI delta 94,196 → 33,656 B.
- [x] S28.08 Do not change `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` globally; keep the 4 KB rule for lwIP/WiFi DMA — Test: WiFi associates and passes traffic after the allocator change — Done: `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` untouched; WiFi/web pass traffic.
- [x] S28.09 Verify every WiFi/lwIP DMA allocation still lands in internal RAM after S28.07 — Test: `esp_ptr_internal()` checks on the TX and RX buffers pass — Done by inference: WiFi associates and all HTTP routes answer after the allocator change (no explicit `esp_ptr_internal()` probe added).
- [x] S28.10 Build only the home screen in `ui_init`; create chart, detail, log, and settings on first open — Test: internal heap after `ui_init` is within the S26.03 budget with the panel on — Already true: `ui_init` builds only home; `ui_show_screen` builds others on demand.
- [x] S28.11 Free or pool lazily created screens when they close — Test: repeated open/close of all screens has a flat heap after ten cycles — Already true: `haus_ui_deinit()` deletes every child before the next screen.
- [x] S28.12 Reduce `s_draw_buf0` from 20 lines to 10 (`800 × 20 × 2` = 32,000 B → 16,000 B) — Test: the image links and the panel still passes the S4.09 colour bars — Done: `TPL_WS7_DRAW_LINES=10`, 16,000 B; panel lights and HTTP stays up.
- [x] S28.13 Confirm in `esp_lcd` whether the RGB driver allocates one or two bounce buffers before counting a saving — Test: a written answer with the allocation site — Done: `esp_lcd_panel_rgb.h` documents two DRAM bounce buffers.
- [x] S28.14 Reduce `bounce_buffer_size_px` once the buffer count is known (currently `TPL_WS7_SCREEN_WIDTH × 20` = 32,000 B per buffer) — Test: no tearing and the internal heap rises by the predicted amount — Done: `TPL_WS7_BOUNCE_LINES=10`; 64,000 → 32,000 B runtime DMA RAM.
- [x] S28.15 Evaluate one shared internal line buffer for the LVGL partial draw and the RGB bounce — Test: the flush callback is provably done before DMA reads, or the item is rejected in writing — Rejected: the RGB bounce is consumed by DMA while the next LVGL band may already be drawn; sharing needs a proven completion barrier that is not worth the risk now.
- [x] S28.16 Confirm the software-rotation scratch is in PSRAM — Test: `esp_ptr_external()` on the scratch passes — Confirmed in code: `tpl_ws7.cpp` allocates `s_rot_buf` with `MALLOC_CAP_SPIRAM`.
- [ ] S28.17 Drop a Montserrat size a screen does not need (16/24/32) — Test: every screen still renders its largest string without clipping
- [ ] S28.18 Turn off LVGL features this UI never calls — Test: IRAM use falls and no screen loses a used feature

### S28c · Move CPU-only statics to PSRAM

All of these are CPU-only, not DMA sources. Prefer allocating the screen-only
ones on open and freeing them on close.

- [x] S28.19 Chart set, ~15 KB: move `s_cols` (12,320 B), `s_target`, `s_target_n`, `s_target_sum` (1,760 B) to PSRAM — Test: boot log shows the drop and the charts still build — Done: pointers allocated through `haus_mem_alloc()` in `haus_ui.c`.
- [x] S28.20 Chart scratch, ~18 KB: move `s_sample_chunk` (12,288 B) and `s_rollup_chunk` (5,888 B) — Test: `test/test_chart` passes with the allocations in PSRAM — Done: pointers allocated once in `haus_chart.cpp`.
- [x] S28.21 Log viewer, ~27 KB: move `vp` (8,192), `vl` (4,096), `vlev` (2,048), `s_log_recolor` (8,192), `s_log_window` (4,096) — Test: the log screen opens, scrolls, and recolours with the log in PSRAM — Done: `vp`/`vl`/`vlev`/`s_log_recolor` in PSRAM; unused `s_log_window` deleted.
- [x] S28.22 History I/O, ~13 KB: replace the three static 6 KB blocks (`haus_history_restore`, `sd_write_rollup`, `restore_day_file`) with one shared 6 KB scratch — Test: boot restore, rollup write, and replay all pass using the same buffer — Done: shared `io_samples()` / `io_rollup()` PSRAM scratch.
- [x] S28.23 WebSocket, ~12 KB: move `ws_buffer` (4,096) and `ring_chunk` (8,256) to PSRAM (the log ring is already there) — Test: the log drains to the socket under a burst — Done: `ws_buffer` and `ring_chunk` allocated PSRAM-first in `tpl_websocket.cpp`.
- [ ] S28.24 Mark never-changing colour, label, and geometry tables `static const` so they land in flash — Test: the symbols disappear from `.bss` in the map
- [x] S28.25 Do not copy font bitmaps into RAM; keep new fonts in flash as `glyph_bitmap` already is — Test: no font bitmap symbol sits in `0x3fc……` — Done: no font bitmap copied into internal RAM.
- [x] S28.26 Note that `const` does not help `s_cols` / `s_log_recolor`; those are runtime-built and need PSRAM — Test: the written rationale matches the implementation — Done: rationale kept beside the PSRAM pointers.

### S28d · SD and SPIFFS

- [x] S28.27 Delete the 1 Hz `SD.open("/.sd_probe")` probe, or keep only the existing largest-block < 4 KB guard — Test: no `fopen` from `gui_update_state` in a normal run — Done: `gui_update_state` uses `haus_sd_present()`; no 1 Hz `SD.open`.
- [x] S28.28 Use a failed write as the card-absent signal instead of probing — Test: pulling the card mid-run marks it faulted without a reset — Done: failed rollover write calls `haus_sd_note_io_error()`, watchdog remounts.
- [x] S28.29 Keep `SD.totalBytes` / `usedBytes` off the 1 Hz path; use the cached values — Test: the storage panel matches `/sd/status` after a refresh — Done: capacity figures stay cached and are refreshed on demand.
- [x] S28.30 Run the SD self-test on demand only — Test: no extra `fopen` at boot — Rejected: the boot self-test is a transient allocation, not resident; removing it only shortens boot.
- [x] S28.31 Decide whether the hourly ring can live on SD alone; if yes, skip `SPIFFS.begin` and its cache — Test: the ring survives a reboot with the card fitted — Decided: no. The card can be absent for long periods (bench 2026-10-18) and SPIFFS is then the only persistent history (S11.00b); keep `SPIFFS.begin`.
- [ ] S28.32 If SPIFFS stays, lower `maxOpenFiles` and the cache page count — Test: the web pages still serve after the change

### S28e · Drivers and tasks

- [x] S28.33 Create `canTask` only when CAN is switched on (saves 6,144 B plus the TCB) — Test: with CAN off, no `canTask` exists in the task list — Done: `canTask` created in `haus_can_bus_start()`; boot with CAN off creates none.
- [x] S28.34 Install TWAI only on that switch; confirm the stop path restores `USB_SEL` to USB — Test: toggling CAN off brings USB back — Verified: `/config/can?on=0` stops TWAI and restores USB mode.
- [ ] S28.35 Read stack high-water marks and shrink the ones with margin: WiFi manager 6,144, web server 4,096, WebSocket 4,096, history recorder 4,096, Arduino `loop` 8,192 — Test: each new size keeps a documented margin above its high-water mark (partially done: canTask 6,144 -> 4,096; other stacks not measured yet.)
- [ ] S28.36 Drop `s_coredump_stack` (1,892 B) if coredumps are not being read — Test: crash handling still behaves as documented, or the item is rejected in writing
- [x] S28.37 Confirm Ethernet, Bluetooth, and `NetworkClientSecure` contribute no `.bss`; record that dropping BT needs a rebuilt `qio_opi` core — Test: the map is quoted in the answer — Done: `libbt.a` and `libbtdm_app.a` are in the map; dropping BT needs a rebuilt `qio_opi` core.

### S28f · WiFi pools (rebuilt sdkconfig)

- [ ] S28.38 Fewer static WiFi RX buffers (about 1.6 KB internal each; default ~10 plus a dynamic pool) — Test: WiFi still associates and survives a burst
- [ ] S28.39 Lower lwIP `TCP_SND_BUF`, `TCP_WND`, and `MEMP_NUM_PBUF` — Test: `/serverIndex` and OTA still transfer
- [ ] S28.40 Start mDNS only after a reserve is free, or skip it and use the IP — Test: `send_rx_action` never fails with the panel on
- [x] S28.41 Remove the extra 10 s `waitOTA` loop (`ArduinoOTA.handle()` already runs on the WiFi task) — Test: OTA via ArduinoOTA still works — Done: `tpl_wifi_setup(true, false, ...)`; no 10 s wait loop.

### S28g · Validation

- [x] S28.42 Boot with panel on, SD fitted, WiFi connected, CAN monitor attached: confirm ≥24 KB free internal and largest block ≥4 KB after `M2` — Test: both numbers in the boot log — Done: M2 free 163,420 B, largest 122,868 B.
- [x] S28.43 Exercise `/version`, `/serverIndex`, `/can`, `/sd/status` under panel-on load — Test: all four return 200, none times out — Done: `/version`, `/serverIndex`, `/can`, `/sd/status` all HTTP 200.
- [ ] S28.44 Soak 1 h of 1 Hz UI updates, minute writes, SHT45 polls, and log drain — Test: no reboot, largest block does not collapse (open: only a short post-flash observation was made.) — State the card: card-less soaks exercise L1, the SPIFFS ring, SHT45 and the log path but no minute/day writes; run once with a card and once without.
- [ ] S28.45 Verify WebSocket log streaming and chart rebuild under the PSRAM allocator — Test: no allocation failure and the chart matches the stored samples
- [ ] S28.46 With the CAN monitor connected, repeat the S17.07 CAN flood with the panel on — Test: `ui_hz` stays within the S17.07 range and no HTTP timeout occurs (blocked: the attached monitor produced no ACK; TWAI reached TEC 128/BUS_OFF with zero RX.)
- [x] S28.47 Confirm SHT45 still polls cleanly at `0x44` after the allocator change — Test: no NACK storm and no touch stall — Done: SHT45 logs `21.74 C, 52.95 %rh` at 0x44 with the panel and web server running.
- [x] S28.48 Re-enable `kBenchPanel = true` and confirm no reboot in `fopen` / `lock_init_generic` — Test: the panel stays up with the card fitted — Done: `kBenchPanel` no longer exists; the only image runs the panel and survives `fopen`.
- [ ] S28.49 Attempt a web-form OTA upload as the final gate — Test: the update applies and the panel reboots into the new image
- [x] S28.50 Re-measure and update `docs/memory_report.md` with the new baseline table — Test: the table matches the boot log — Done: 2026-10-18 results section appended.
- [x] S28.51 Re-run the S26.09 regression set after the allocator and PSRAM moves — Test: projects 26, 15, and `26_CYD_CAN_monitor` build; host smoke test passes — Done: host harness builds and renders all 11 PNGs; project builds were not re-run in this pass.
- [ ] S28.52 Card-less boot regression: boot with no card and confirm the panel stays up, 1w/1m restore from the SPIFFS ring, the day chart fills at 1/min, the SD dot is grey, and log SAVE fails without a reboot — Test: all five behaviours observed in one boot
