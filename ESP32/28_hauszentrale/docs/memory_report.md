# Internal SRAM report

Date: 2026-10-18. Board: Waveshare ESP32-S3 Touch LCD 7, MAC `28:84:85:a7:a6:78`,
8 MB PSRAM, 16 MB flash. USB console `/dev/cu.usbmodem5C941561291`.

The chip has 512 KB of on-chip SRAM. The application never receives that as a
heap. After caches, instruction RAM, and static data, the panel and WiFi are
competing for about 166 KB, and together they overshoot it. The visible
failure is a reboot in `fopen`, or HTTP that never answers.

A measurement image with the panel skipped was flashed during the
investigation. On that image HTTP answered and the display stayed dark.
This checkpoint puts the panel back on. Without moving the UI off internal
RAM, that returns the 1 KB heap. The `fopen` guard and the `M1`/`M2` logs
stay, so the chip no longer reboots when the probe cannot allocate a lock.

## What failed

Bootloader and partition table were read back and are intact. The second-stage
bootloader at `0x0` matches `.pio/build/esp32usb/bootloader.bin` (ESP-IDF
v5.5.5, 19,968 bytes, checksum and SHA-256 valid). The reboot is the
application.

With the panel on and the SD card in, the log reached WiFi, wrote the day
file and the rollup, then:

```
mdns_networking: Cannot allocate memory (send_rx_action)
abort() was called at PC 0x403777bf on core 1
```

Decoded against the USB ELF `268e0b2dc` (the image that was running):

| PC | Function |
| --- | --- |
| `0x403777bf` | `lock_init_generic` (`locks.c:77`) |
| `0x4210ee7d` | `fopen` |
| `0x4200b8d9` | `VFSFileImpl` constructor |
| `0x4204901a` | `haus_sd_is_mounted` (`haus_sd.cpp`, the `SD.open` probe) |
| `0x42047021` | `gui_update_state` |
| `0x4204a71c` | `loop` |

`lock_init_generic` aborts when it cannot allocate the newlib `FILE` lock.
That allocation is internal RAM. mDNS had already failed the same way one
tick earlier. The free-heap number in the mDNS line (~6.2 MB) is the total
heap, PSRAM included. Internal RAM was gone.

`haus_sd_is_mounted` runs from the 1 Hz UI update and opens `/.sd_probe`.
A missing card returns a failed `open`. An empty internal heap never gets
that far: `fopen` aborts the chip.

## Where the 512 KB goes

From `framework-arduinoespressif32-libs/esp32s3/ld/memory.ld` and the
`qio_opi` `sdkconfig.h` (IDF 5.5.5). The comment in that linker script is
the map:

```
40370000  IRAM / ICache   40378000  D/IRAM (instruction view)  403E0000
          3FC88000  D/IRAM (data view)  3FCF0000  DRAM / DCache  3FD00000
```

| Region | Size | Role |
| --- | --- | --- |
| Instruction cache | 16 KB | `CONFIG_ESP32S3_INSTRUCTION_CACHE_16KB`. Not usable as RAM. |
| Instruction SRAM | 16 KB | `0x40374000`–`0x40378000`. Vectors and IRAM. Link is at 100%. |
| Data cache | 32 KB | `CONFIG_ESP32S3_DATA_CACHE_32KB`. PSRAM and flash data. Not usable as RAM. |
| DRAM under the data cache | 32 KB | `0x3FCF0000`–`0x3FCF8000`. Runtime heap only, not available for `.bss`. |
| DIRAM | 416 KB | Shared. Instruction view `0x40378000`, data view `0x3FC88000`. |

The DIRAM the linker will place static data in is `dram0_0_seg`:
`0x3FC88000`, length `0x53700` (341,760 B). It ends at `0x3FCDB700`
(instruction alias `0x403CB700`). The rest of DIRAM, from there to
`0x403E0000` (84,224 B), is reserved for startup and added to the heap
after the app starts. The comment in `memory.ld` says that range cannot
hold static allocations.

Inside the 341,760 B window, on the panel-off image:

| Occupant | Bytes |
| --- | --- |
| DIRAM used as instruction code (`0x40378000`–`0x40388b00`) | 68,352 |
| `.data` | 23,756 |
| `.bss` | 118,760 |
| Linker heap from `_bss_end` (`0x3FCBB7B8`) to the end of the segment | 130,888 |

The panel-on image adds `s_draw_buf0[800 * 20 * 2]` = 32,000 B of `.bss`.
That comes straight out of the heap. Measured baseline fell from 198,440 B
to 165,560 B (32,880 B; the extra 880 B is other panel-only static that the
compiler drops when the panel path is dead).

`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` is 4096. Every allocation of 4 KB or
less stays in internal RAM. PSRAM only receives larger blocks. WiFi buffers,
mDNS packets, newlib `FILE` locks, and most LVGL objects are under that
line, so the 8 MB of PSRAM cannot absorb them.

## Measurements

Two boots of the measurement firmware (`master-cc3410ac`), SD card removed,
CAN off in config. The second boot is the same image with the panel and
`ui_init` skipped.

| Point | Panel on | Panel off |
| --- | --- | --- |
| Baseline, before the panel | 165,560 B | 198,440 B |
| After `ui_init` | 62,624 B | panel not started |
| `lv_init` part of that drop | 8,740 B | |
| Display + UI part (`S26.03` delta) | 94,196 B | |
| After WiFi (`M1`) | 4,036 B, largest block 2,164 B | 133,696 B, largest block 90,100 B |
| After the web server (`M2`) | 1,244 B, largest block 948 B | 126,184 B, largest block 86,004 B |
| `curl` `/version` `/serverIndex` `/can` `/sd/status` | timed out (ping answered) | HTTP 200 |

WiFi plus the web server cost about 59 KB when the panel had already taken
its share (62,624 → 1,244), and about 72 KB when the panel was absent
(198,440 → 126,184). The difference is pools that shrink when `malloc`
starts failing. mDNS is the first of those failures.

PSRAM across `ui_init` on the panel-on boot dropped by 1,007,036 B. One
800×480 RGB565 frame is 768,000 B. The framebuffer is in PSRAM
(`fb_in_psram = 1`). The shortage is not the frame.

Arithmetic that matches the table: 166 KB at the start of the panel, minus
103 KB for `lv_init` + UI, minus ~60 KB for WiFi, is zero. The measured
remainder was 1,244 B.

## What the 94 KB of UI is

* Bounce buffer. `tpl_ws7.cpp` sets `bounce_buffer_size_px` to
  `TPL_WS7_SCREEN_WIDTH * 20`. Width is 800, so one buffer is
  `800 × 20 × 2` = 32,000 B of internal DMA RAM. It cannot move to PSRAM.
  The RGB driver may allocate a second buffer for DMA ping-pong; confirm in
  `esp_lcd` before counting 64 KB. This allocation is inside the 94,196 B.
* LVGL objects, styles, and small buffers. They are almost all under 4 KB,
  so the C library allocator keeps them internal. Large images already go
  to PSRAM, which is why PSRAM moves by a megabyte while internal RAM still
  loses 94 KB.
* `lv_init` itself, 8,740 B, counted separately from the 94,196 B.

The 32,000 B draw buffer is not part of the 94 KB. It is `.bss`, already
reflected in the lower baseline.

## Static internal residents

Largest `.bss` / `.data` symbols in the panel-off ELF. Addresses `0x3fc……`
are internal. Glyph bitmaps are at `0x3c……` (flash) and are already out of
SRAM.

| Symbol | Bytes | Where | When it is needed |
| --- | --- | --- | --- |
| `s_cols` | 12,320 | `haus_ui.c` | Chart on screen. 440 × `ChartColumn`. |
| `s_sample_chunk` | 12,288 | `haus_chart.cpp` | Chart rebuild. |
| `ring_chunk` | 8,256 | `tpl_websocket.cpp` | WebSocket log drain. |
| `vp` | 8,192 | `haus_ui.c` | Log screen line table. |
| `s_log_recolor` | 8,192 | `haus_ui.c` | Log screen. |
| `s_rollup_chunk` | 5,888 | `haus_chart.cpp` | Chart rebuild. |
| `haus_history_restore` block | 5,888 | `haus_history.cpp` | Boot restore. |
| `sd_write_rollup` block | 5,888 | `haus_history.cpp` | Daily rollup write. |
| `s_log_window` | 4,096 | `haus_ui.c` | Log screen. |
| `vl` | 4,096 | `haus_ui.c` | Log screen line table. |
| `ws_buffer` | 4,096 | `tpl_websocket.cpp` | WebSocket JSON. |
| `port_IntStack` | 4,192 | FreeRTOS | Interrupt stack. Leave it. |
| `g_cnxMgr` | 3,880 | WiFi | Driver. Leave it. |
| `vlev` | 2,048 | `haus_ui.c` | Log screen. |
| `s_target_sum` | 1,760 | `haus_ui.c` | Chart. |
| `s_coredump_stack` | 1,892 | coredump | Only if dumps are read. |
| `restore_day_file` block | 1,536 | `haus_history.cpp` | Boot replay. |
| `s_draw_buf0` | 32,000 | `main.cpp` | Panel on only. Not in this ELF. |

`canTask` is 6,144 B plus its TCB, created in `haus_can_bus_init` before the
baseline log, including when config says CAN off. It is already absent from
the 165,560 B figure.

## SD card

The card is not the cause. Removed, the mount fails cleanly (`Card Failed`,
then `mount failed`) and the chip stays up. The reboot needs the panel's
RAM use plus a later `fopen`.

Read on the Mac (`/Volumes/NO NAME`):

| File | Size | Contents |
| --- | --- | --- |
| `data_20261017.bin`, `data_20261018.bin` | 34,652 B | 34,560 B of minute samples plus 23 CRC32s. Matches the firmware log `day file written … (34652 B)`. |
| `rollup.bin` | 68,496 B | 68,448 B of rollup columns plus 12 CRC32s. |

## HTTP and OTA

Routes that answered with the panel off, at `192.168.1.146`:

| Path | Result |
| --- | --- |
| `/version` | 200, `{"git_hash":"cc3410ac",…}` |
| `/serverIndex` | 200, the SPIFFS OTA page |
| `/can` | 200, `running: false`, heap 111,396 B |
| `/sd/status` | 200, `type: ABSENT` while the card was in the Mac |

With the panel on, the same URLs timed out. The server task exists, and
ICMP works, but 1 KB cannot allocate a request buffer. `/serverIndex` also
404s when SPIFFS failed to mount; after `uploadfs` the file is there.

OTA via the web form needs that page and a living server. ArduinoOTA is
started inside `tpl_wifi_setup`, and `ArduinoOTA.handle()` runs on the WiFi
task. The extra 10 s `waitOTA` loop in setup does not hold RAM. It is the
window in which the heap reaches zero. An upload was not attempted.

## Proposals

Ordered by how much internal RAM they return. None of these are done, except
the `fopen` guard and the `M1`/`M2` logs, which are in the tree for the
measurement.

### Panel and LVGL

1. Custom LVGL allocator (`LV_STDLIB_CUSTOM`) that takes PSRAM first.
   Widgets do not need DMA. This is the change that lets the panel and WiFi
   coexist. Do not lower `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` globally:
   lwIP and WiFi DMA buffers would land in PSRAM and break.
2. Build only the home screen in `ui_init`. Create chart, detail, log, and
   settings on first open. `ui_init` is the 94 KB step.
3. Fewer draw-buffer lines. Twenty lines is 32,000 B of `.bss`. Ten lines
   returns 16 KB before `main`. The jitter note used an 8-line porch as the
   timing reference; the draw buffer is a separate knob from that porch.
4. Fewer bounce-buffer lines. Same 32,000 B per buffer, and it must stay
   internal DMA. Confirm whether `esp_lcd` allocates one buffer or two
   before counting the saving.
5. One internal line buffer shared by the LVGL partial draw and the RGB
   bounce, if the flush callback has finished with it before DMA reads it.
6. Keep the software-rotation scratch in PSRAM. It is already there.
7. Drop a Montserrat size if a screen does not need 16, 24, and 32. Glyphs
   are already in flash; this saves flash and cache traffic, not `.bss`.
8. Turn off LVGL features this UI never calls. Shrinks IRAM and the small
   internal objects.

### Move these structs to PSRAM

CPU-only. None of them are DMA sources. Prefer allocating the screen-only
ones when the screen opens and freeing them when it closes.

* Chart set, ~15 KB: `s_cols`, `s_target`, `s_target_n`, `s_target_sum`.
* Chart scratch, ~18 KB: `s_sample_chunk`, `s_rollup_chunk`.
* Log viewer, ~27 KB: `vp`, `vl`, `vlev`, `s_log_recolor`, `s_log_window`.
* History I/O, ~13 KB: the three static blocks in `haus_history.cpp`. One
  shared 6 KB scratch replaces all three.
* WebSocket, ~12 KB: `ws_buffer`, `ring_chunk`. The log ring is already in
  PSRAM.

### `const`

A non-const static lives in internal `.bss` for the whole run. `static
const` goes to flash and is read through the data cache.

* Color tables, label tables, and screen geometry that never change.
* Do not copy font bitmaps into RAM. New fonts should stay as the existing
  `glyph_bitmap` objects do.
* `const` does not apply to `s_cols` or `s_log_recolor`. Those are built at
  runtime. PSRAM does.

### SD and SPIFFS

* Delete the 1 Hz `SD.open` probe, or keep the guard that skips it when the
  largest internal block is under 4 KB (that guard is in `haus_sd.cpp`
  now). A failed write is the signal that the card is gone. `fopen` on an
  empty heap reboots; it does not return an error.
* Keep `SD.totalBytes` / `usedBytes` off the 1 Hz path. They are already
  cached for the storage panel.
* Run the SD self-test only on demand. It is an extra `fopen` at boot, not
  a resident cost.
* If the hourly ring can live on the SD card alone, skip `SPIFFS.begin` and
  its cache (a few kilobytes, visible once the image started mounting).
  If the ring must survive a missing card, keep SPIFFS and lower
  `maxOpenFiles` and the cache page count.
* The web pages are small. They are not the 94 KB. They 404 when SPIFFS is
  not mounted, and they hang when internal RAM is at 1 KB.

### Drivers and tasks

* Create `canTask` only when CAN is switched on. 6 KB plus the TCB, today
  taken even with CAN off. It does not by itself make room for the panel.
* Install TWAI only on that switch. A failed start used to leave `USB_SEL`
  on the transceiver; the stop path now restores USB.
* Read stack high-water marks and shrink the ones with margin: WiFi manager
  6,144, web server 4,096, WebSocket 4,096, history recorder 4,096, Arduino
  `loop` 8,192. Those stacks are internal RAM.
* Confirm Ethernet, Bluetooth, and `NetworkClientSecure` contribute no
  `.bss`. The link map lists them as inputs. Dropping BT means a rebuilt
  `qio_opi` core, not a `platformio.ini` flag.
* Drop the coredump stack (1,892 B) if dumps are not being read.

### WiFi pools

Shrink these in a rebuilt sdkconfig. Do not change the 4 KB internal-malloc
rule.

* Fewer static WiFi RX buffers. Each is about 1.6 KB internal. The usual
  default is 10 static plus a dynamic pool.
* Smaller lwIP `TCP_SND_BUF`, `TCP_WND`, and `MEMP_NUM_PBUF`.
* Start mDNS only after a reserve is free, or skip it and use the IP.
  `send_rx_action` / `mdns_parse_packet` are the calls that already failed.
* `waitOTA` can go. `ArduinoOTA.handle()` already runs on the WiFi task.

### Reserve

* Keep logging free internal RAM and the largest block after WiFi and after
  the web server (`M1`, `M2` in `main.cpp`).
* Treat about 24 KB free, and a largest block of at least 4 KB, as the
  minimum before `fopen`, HTTP, or mDNS. Below that, skip the call.
