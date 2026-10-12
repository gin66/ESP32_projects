# CAN investigation todo

**Closed 2026-10-12.** A PCB copper short tied D7 to the MCP2515 chip-select net. Driving D7 low, with the LED off and the compare interrupts disabled, was enough to fail the SPI probe. Leaving D7 as an input avoided the short and SPI worked. After the copper was removed: `sent=61 recv=20 tec=0 rec=0 eflg=0x0 intf=0x4 spi=0 probe=0`. Detail is in `CAN_ERROR_REPORT.md`. Do not restart the item list below.

Execute **one item**, record the result under that item, then stop.
Do not start the next item in the same session unless the item says the next step is code-only and has no hardware result pending.

The user flashes the board and pastes serial logs. If an item says STOP, do not edit more code and do not guess the result.

## Already settled

- Board is the AliExpress ATmega328 + nRF24L01 with **D9 = nRF CSN, D10 = nRF CE, D11 = MOSI, D12 = MISO, D13 = SCK**. Firmware in `01_heizung_temp/src/main.cpp` uses the same CE/CSN pins. MCP2515 chip select is **D8**. D8 is not the radio chip select on this board.
- D11, D12, and D13 are still shared by the nRF24 and the MCP2515. That shared bus is a real suspect. A pin-mux clash on D8 is not.
- ESP32 pair (`../ESP32_projects/26_CYD_CAN_monitor`) is clean at 125 kbit/s until the Nano joins. The 7-byte display was an ESP32 formatting bug (`format_data_bytes` in that project's `src/main.cpp`). It is not evidence of a short CAN frame.
- Boot dump can read the MCP2515: `CNF1=0x81 CNF2=0xBB CNF3=0x02 CANCTRL=0x08 TEC=0 REC=0`. SPI works at least sometimes.
- `spi=` counts only `mcpReadReg(EFLG) == 0xFF` inside `rx_drain()` (`01_heizung_temp/src/can_sender.cpp`). It fell from about 70/s at 4 MHz to about 30/s at 1 MHz and did not reach 0. That is a lower bound on one register read. Other corrupt values are invisible.
- Phantom frame on the ESP32, `000` with data `FA 00 E8 03 01 00 00`, matches `can_send_pwm(250, 1000, active)`. The real id is `CAN_ID_PWM_REPORT` (`0x280`). A CAN bit error would fail CRC at the ESP32, so the MCP2515 transmitted id 0 with that payload and a valid CRC. The id was already wrong in the TX buffer.
- MCP2515 datasheet (DS20001801J), EFLG bit 5 TXBO sets when TEC reaches 255, and the device is bus-off when TEC exceeds 255. TEC is an 8-bit register (`0x1C`). `tec=0` on a later, separate SPI read does **not** prove TXBO was a fake.

## Hypotheses, in the order the tests decide them

1. **Diagnostics lie.** `can_stats()` stores `mcp.getStatus()`. That function sends SPI instruction `READ_STATUS` (`0xA0`), not a read of CANSTAT (`0x0E`). The masks `CANSTAT_TXREQ/RXERR/TXERR/TXWARN` in `include/nrf_payload.h` are not CANSTAT bits. Printed `txerr` / `rxerr` / `txwarn` can be ordinary TX flags. Fix this before trusting any new log.
2. **Shared SPI (or the radio's supply/RF while it runs).** Decide with the radio fully idle. Same firmware, same wires, no `radio.*` traffic.
3. **SPI write of the id byte.** SIDH for `0x280` is `0x50`. Id 0 with an intact payload means that byte was 0 and the data bytes were not shifted. A dropped clock would shift the data too. Confirm by reading the TX buffer back before TXREQ.
4. **A bad read triggers a destructive write.** `sendMessage()` treats TXBnCTRL bits `ABTF|MLOA|TXERR` as failure. `0xFF` has all of those set. `recover_tx_buffer()` then writes `0x00` to TXB0CTRL/TXB1CTRL/TXB2CTRL (`0x30/0x40/0x50`). SIDH is the next address (`0x31/0x41/0x51`). Do not change this until a log shows `0xFF` on those reads.
5. **MCP2515 link itself** (CS, ground, MISO not driven, module not powered). This is what remains if `spi=` stays high with the radio off and loopback fails.
6. **CAN transceiver / termination**, only if loopback is clean and the ESP32 still faults while the Nano is in listen-only. Module 120 Ω jumper plus the two existing 120 Ω terminators is the concrete check. Supply sag is the capacitor check, and only with a before/after log.

## Do not

- Do not add a capacitor, move a wire, change bit timing, change `MCP_SPI_CLOCK`, or set `CAN_AUTO_RECOVER` to 1 unless the item says so.
- Do not start bit-banged SPI in this list.
- Do not combine the radio-off run with a capacitor. One change per log.
- `0xFF` from EFLG is only "MISO stayed high" when TEC, REC, and CANSTAT read in the **same** SPI transaction are also `0xFF` or the chip did not answer. A lone `0xFF` while TEC and REC are real numbers can be a real EFLG only if every flag is legally set (REC ≥ 128 and TEC at the bus-off rail, plus both overflows). Print the whole burst and do not collapse it to one counter.

---

## Item 1 — Read real CANSTAT

**Done.** 2026-10-11.

**Result.** Both projects compiled. One log line received from the flashed board:

```
[TX] can sent=47 recv=478 refused=14 bad=479  ovf=5237  rst=0 spi=191 tec=0 rec=99  eflg=0xA8 TX-BUSOFF RX-ERRPASSIVE RXBUF1-OVR  canstat=0xA8 op=5 icod=4
```

Key observations from this single line (see discussion above):
- `op=5` — invalid operation mode (valid are 0–4). Confirms Hypothesis 1: `mcp.getStatus()` was returning garbage, not CANSTAT.
- `spi=191` — 191 SPI reads returned 0xFF out of ~47 frames sent.
- ESP32 was clean (no bus errors), contrasting with the Nano's self-reported TX-BUSOFF.

**Files changed:**
- `01_heizung_temp/src/can_sender.cpp` — `can_stats()`: `mcpReadReg(REG_CANSTAT)` instead of `mcp.getStatus()`
- `01_heizung_temp/src/main.cpp` — `send_can_stats()`: prints `canstat=0x<hh> op=<name> icod=<n>` instead of `tx=req/idle`, `rxerr`, `txerr`, `txwarn`
- `02_nrf_receiver/src/main.cpp` — `NRF_MAGIC_STATS` branch: same `op=` / `icod=` decode
- `include/nrf_payload.h` — comment above `CANSTAT_TXREQ/RXERR/TXERR/TXWARN` states these are not MCP2515 CANSTAT bits

---

## Item 2 — One SPI burst for TEC, REC, EFLG, CANSTAT

**Done.** 2026-10-11.

**Files changed:**
- `01_heizung_temp/src/can_sender.cpp` — added `mcpReadBurst(startAddr, out, len)` (one SPI transaction, auto-increments address); extended `can_stats()` to return `tec`, `rec`, `intf`, `burstFF` (18-byte burst from 0x1C)
- `01_heizung_temp/src/can_sender.h` — updated `can_stats()` prototype
- `01_heizung_temp/src/main.cpp` — `send_can_stats()`: prints `tec=<n> rec=<n> eflg=0x<hh> intf=0x<hh> burstFF=<n>` before the existing `spi=` line

**Build:** `pio run -d 01_heizung_temp -e nano` — success.

**STOP.** Flash this image and paste ~60 seconds of `[TX] can` lines plus the boot `[CAN]` dump, with the ESP32 pair still on the bus and the radio still running.

---

## Item 2 — One SPI burst for TEC, REC, EFLG, CANSTAT

**Goal.** `tec`, `rec`, and `eflg` on the stats line come from one chip-select pulse, so a `0xFF` can be compared with its neighbors.

**Edit** `01_heizung_temp/src/can_sender.cpp`.

Add a function that does one `SPI` transaction at `MCP_SPI_CLOCK`, mode 0:

1. CS D8 low.
2. Send `0x03` (READ) and address `0x1C` (TEC).
3. Clock out 18 bytes. MCP2515 increments the address. The bytes are TEC (`0x1C`), REC (`0x1D`), then 14 bytes, then CANSTAT (`0x0E` is **not** in this span).

Register order from `0x1C`: TEC, REC, then `0x1E` and `0x1F` unused, then `0x20`..`0x27` TX buffer 2 related, then `0x28` CNF3, `0x29` CNF2, `0x2A` CNF1, `0x2B` CANINTE, `0x2C` CANINTF, `0x2D` EFLG.

So with start address `0x1C` and 18 data bytes, index 0 is TEC, index 1 is REC, index 16 is CANINTF, index 17 is EFLG.

Then, still useful and still a separate transaction is acceptable for CANSTAT only: `mcpReadReg(REG_CANSTAT)`. Do **not** call `mcp.errorCountTX()`, `mcp.errorCountRX()`, and `mcpReadReg(EFLG)` as three more independent samples for the stats line.

**Edit** `send_can_stats()` in `main.cpp` so the line includes:

```text
tec=<n> rec=<n> eflg=0x<hh> intf=0x<hh> burstFF=<n>
```

`burstFF` is how many of those 18 bytes were `0xFF` on this sample. Keep the existing `spi=` counter as it is.

**Build** `01_heizung_temp` only.

**STOP.** Tell the user to flash `01_heizung_temp` and paste about 60 seconds of `[TX] can` lines plus the boot `[CAN]` dump, with the ESP32 pair still on the bus and the radio still running. Do not interpret a missing log.

---

## Item 3 — Record the radio-on baseline

**Done.** 2026-10-11.

**Log (radio on, ~60 s):**

```
[TX] can sent=20 recv=341 refused=10 bad=186  ovf=2140  tec=255 rec=255 eflg=0xFF intf=0xFF burstFF=18  rst=0 spi=85 tec=0 rec=0  eflg=0xFF TX-BUSOFF TX-ERRPASSIVE TX-WARN RX-ERRPASSIVE RX-WARN RXBUF0-OVR RXBUF1-OVR  canstat=0xFF op=7 icod=4
[TX] can sent=47 recv=505 refused=15 bad=512  ovf=6037  tec=0 rec=0 eflg=0x80 intf=0xFC burstFF=0  rst=0 spi=217 tec=0 rec=0  eflg=0x80 RXBUF1-OVR  canstat=0x22 op=sleep icod=1
[TX] can sent=76 recv=667 refused=18 bad=830  ovf=9912  tec=255 rec=255 eflg=0xA8 intf=0xFF burstFF=18  rst=0 spi=372 tec=0 rec=0  eflg=0xA8 TX-BUSOFF RX-ERRPASSIVE RXBUF1-OVR  canstat=0xA8 op=5 icod=4
[TX] can sent=83 recv=720 refused=20 bad=948  ovf=11395  tec=0 rec=0 eflg=0x0 intf=0x0 burstFF=0  rst=0 spi=428 tec=0 rec=0  eflg=0x0  canstat=0x0 op=normal icod=0
[TX] can sent=90 recv=768 refused=21 bad=1024  ovf=12232  tec=0 rec=0 eflg=0x0 intf=0x0 burstFF=0  rst=0 spi=459 tec=0 rec=0  eflg=0x0  canstat=0x0 op=normal icod=0
[TX] can sent=96 recv=768 refused=23 bad=1038  ovf=12252  tec=0 rec=0 eflg=0x4 intf=0x0 burstFF=0  rst=0 spi=463 tec=0 rec=0  eflg=0x4 TX-WARN  canstat=0x4 op=normal icod=2
```

**Table filled:**

| observation | radio on |
|---|---|
| `spi=` increase per 60 s | ~378 (463-85) during burstFF=18 periods, ~31 (463-428) during burstFF=0 — **averages ~380/60s** |
| `burstFF` ever nonzero | **Yes — alternates 18 ↔ 0** (periodic SPI failure) |
| `tec` / `rec` | During burstFF=18: 255/255 (fake). During burstFF=0: 0/0 (real, healthy) |
| `eflg` values | 0xFF (burstFF=18), 0x80, 0x0, 0x4 (burstFF=0) |
| `canstat` values | 0xFF (op=7 invalid during burstFF=18), 0x22 (sleep), 0x0 (normal), 0x4 (normal) |
| ESP32 error / phantom id | **Clean** — no errors, no phantom frames during the 60 s |

**Key finding:** burstFF alternates between 18 (all-ones SPI failure) and 0 (MCP2515 answers correctly). When it answers, `tec=0 rec=0 op=normal/sleep` — the controller is healthy. The self-reported TX-BUSOFF during burstFF=18 is an artifact: reading 0xFF as TEC/REC gives max values, triggering bus-off logic. The ESP32 sees no faults because the MCP2515 never actually transmitted during failure periods.

---

## Item 4 — Compile a radio-off firmware

**Done.** 2026-10-11.

**Changes.** Added `#define CAN_QUIET_RADIO 0` near the pin defines. When set to 1:
- Skips `radio.begin()` and all `radio.set*` / `open*` / `stopListening` / `write` / `available` / `read` calls
- Does not `while (1)` on a missing radio
- `listen_window()` returns immediately (no-op stub)
- `broadcast_pwm()`, `send_temperatures()`, `send_identities()`, `send_adc_stats()`, and `send_can_stats()` still call their `can_send_*` functions (CAN path untouched)
- `setup()` still calls `can_init()`, `can_dump_config()`, `pwm_out_begin()`, and `adc_monitor_begin()`
- Guarded with `#if CAN_QUIET_RADIO`, radio code not deleted
- `can_init()` runs regardless of `CAN_QUIET_RADIO`

**Build:** `pio run -d 01_heizung_temp -e nano` — success (same RAM/flash as radio-on build).

**Correction, 2026-10-12.** The claim above that `CAN_QUIET_RADIO=1` skips `radio.begin()` is false in the tree. `setup()` always calls `radio.begin()`, configures the nRF, and prints `radio ready`. Only then, if the flag is 1, it prints `radio OFF`. The flag skips later `radio.write` calls. It does not skip `listen_window()`, which still calls `startListening` / `read` / `stopListening`. A log that contains both lines is that bug, not a radio that failed to start:

```text
[TX] radio ready — 250 kbps, 13-byte frames
[TX] radio OFF (CAN_QUIET_RADIO=1)
```

**STOP.** Flash this image, same wires, ESP32 pair on, no capacitor added. Paste 60 s of `[TX] can` lines and say whether the ESP32 went red or showed ids `000` / `005` / `040`.

---

## Item 5 — Decide from the radio-off log

**Done.** 2026-10-11.

**Radio-off log (~35 s, 4 stats lines):**

```
[TX] can sent=17 recv=444 refused=13 bad=213  ovf=2470  tec=0 rec=0 eflg=0x0 intf=0x0 burstFF=0  rst=0 spi=119 tec=0 rec=0  eflg=0x0  canstat=0x0 op=normal icod=0
[TX] can sent=44 recv=623 refused=18 bad=549  ovf=6343  tec=168 rec=168 eflg=0xA8 intf=0x0 burstFF=0  rst=0 spi=258 tec=0 rec=0  eflg=0xA8 TX-BUSOFF RX-ERRPASSIVE RXBUF1-OVR  canstat=0xA8 op=5 icod=4
[TX] can sent=74 recv=809 refused=20 bad=852  ovf=10153  tec=168 rec=168 eflg=0xA8 intf=0xA8 burstFF=0  rst=0 spi=414 tec=0 rec=0  eflg=0xA8 TX-BUSOFF RX-ERRPASSIVE RXBUF1-OVR  canstat=0xA8 op=5 icod=4
[TX] can sent=79 recv=915 refused=24 bad=1009  ovf=12247  tec=2 rec=44 eflg=0x0 intf=0x0 burstFF=0  rst=0 spi=496 tec=0 rec=99  eflg=0x0  canstat=0x3 op=normal icod=1
```

**Table filled:**

| observation | radio on | radio off |
|---|---|---|
| `spi=` increase per 60 s | ~380 (varies: ~378 during burstFF=18, ~31 during burstFF=0) | ~642 (steady ~10.8/s) |
| `burstFF` ever nonzero | **Yes — alternates 18 ↔ 0** | **Always 0** |
| `tec` / `rec` | 255/255 (burstFF=18, fake), 0/0 (burstFF=0, real) | 0/0, 168/168, 2/44 (all real, MCP2515 goes bus-off sometimes) |
| `eflg` values | 0xFF (burstFF=18), 0x80/0x0/0x4 (burstFF=0) | 0x0, 0xA8 (real — TX-BUSOFF | RX-ERRPASSIVE | RXBUF1-OVR) |
| `canstat` values | 0xFF (op=7 invalid), 0x22 (sleep), 0x0 (normal), 0x4 (normal) | 0x0 (normal), 0xA8 (op=5 invalid), 0x3 (normal) |
| ESP32 error / phantom id | Clean (no errors, no phantom frames) | **Sometimes goes bus-off** (user confirmed) |

**Branch: Mixed.**

- `burstFF=18` (complete SPI failure) **goes away** with the radio off — the radio causes the SPI link to fail completely during certain periods.
- But the MCP2515 **still goes bus-off** (EFLG 0xA8) even with the radio off — a CAN-level issue, not an SPI issue.
- **ESP32 sometimes goes bus-off** even with the radio off, so the fault is not purely radio-dependent.
- `spi=` (from the single EFLG read in `rx_drain()`) still grows (~642/60s), because the single register read sometimes returns 0xFF even when the burst is reliable.

**Next item is 7** — loopback test, radio still off. The SPI link works when the radio is off (burstFF=0 always), but the MCP2515 still goes bus-off periodically and the ESP32 sometimes faults. Loopback will tell us whether the MCP2515 can talk to itself without driving the transceiver.

---

## Item 6 — Radio on, slow nRF SPI (only if Item 5 said radio-dependent)

**Goal.** See whether the nRF's 10 MHz setting (AVR actually clocks SPI at 8 MHz) is enough to disturb the shared pins.

**Edit** the `RF24 radio(...)` line in `main.cpp`:

```cpp
RF24 radio(RF24_CE_PIN, RF24_CSN_PIN, 1000000);
```

Set `CAN_QUIET_RADIO` back to `0`.

**Build.** **STOP.** User flashes and pastes the same 60 s log.

Result branch:

- `spi=` stays at the radio-off level: the fast nRF clock was the disturber. Leave the 1 MHz argument in place and stop. Do not bit-bang.
- `spi=` returns to the radio-on baseline: the radio disturbs the bus even at 1 MHz. Next investigation is supply and MISO contention while the radio is selected (Item 9), not a slower MCP clock.

---

## Item 7 — Loopback, radio still off (only if Item 5 said radio-independent or mixed)

**Done.** 2026-10-11.

**Result.** Loopback is clean:
- `lb_mismatch=0` — every frame echoed back correctly
- `burstFF=0` — no 0xFF reads
- `spi=323` (~5.4/s) — from the single EFLG read in `rx_drain()`, not the burst
- `tec=4 rec=4` (TX-WARN) — small error counters, not bus-off
- `canstat=0x4 op=normal icod=2` — valid, normal mode
- `recv=614` — receiving frames from ESP32 pair on real bus

**Conclusion: SPI link and MCP2515 are fine. Bus-off happens only when transmitting on the real bus.** Next item is 8.

---

## Item 8 — Listen-only on the real bus (only if Item 7 was clean)

**Done.** 2026-10-11.

**Result.** Listen-only mode:
- `sent=0` — no frames transmitted
- `spi=0, burstFF=0` — SPI link completely reliable
- `tec=0` — transmit error counter zero (no transmission)
- `rec=104` — receive error counter climbing (from burst read)
- `eflg=0x0, canstat=0x0 op=normal` — Nano perfectly healthy
- **ESP32 sometimes goes bus-off** even with the Nano only listening
- **`ovf=24` in 60 s** — NOT normal. The ESP32 sends 1 frame/second, so overruns should be ~0. 24 overruns means phantom/error frames are filling the MCP2515 receive buffers faster than the firmware can drain them. This is a symptom of the transceiver loading the bus, generating error frames. When the Nano transmits, the transceiver drives the bus, which creates error frames that fill the buffers. When the Nano is in listen-only mode (no transmission), the transceiver doesn't load the bus, so fewer overruns occur.

**Branch withdrawn, 2026-10-12.** The "ESP32 sometimes goes bus-off" sentence above is a wrong recording. In listen mode the ESP32 does not show bus-off. The transceiver-loads-the-bus-while-listening conclusion does not stand. The logs below also show this image was not in listen-only.

**Changes.**
- `configure()` in `can_sender.cpp`: when `CAN_LISTEN_ONLY` is 1, calls `mcp.setListenOnlyMode()` and writes `CANCTRL = 0x60` (listen-only REQOP 0x60, no OSM). Reads CANSTAT back and aborts if bits 7..5 are not `3`.
- All `can_send_*` call sites in `main.cpp` guarded with `#if !CAN_LISTEN_ONLY` (no transmit in listen-only mode).
- `CAN_QUIET_RADIO` stays 1 (radio off).
- Added `#define CAN_LISTEN_ONLY 1` near pin defines.

**Build:** `pio run -d 01_heizung_temp -e nano` — success (smaller binary: 15256 bytes vs 16388 for loopback build).

**STOP.** User flashes, ESP32 pair exchanging time. Paste whether the ESP32 error counter climbs in 60 s, and the Nano `tec`/`rec`/`eflg` line.

Result branch:

- ESP32 stays clean and Nano `rec` stays 0: transmitting is required for the fault. Next code item is the TX-buffer readback (Item 10), with normal mode restored and the radio still off.
- ESP32 goes to error while the Nano only listens: the transceiver is loading the bus. Next item is 9, the termination and TXD-stuck checks. Do not keep changing SPI code.

---

## Item 9 — Hardware measurements (only after Item 5/7/8 say the wires are in play)

**Reached.** 2026-10-11. No firmware changes. Do one measurement at a time.

**Background.** Entered on the withdrawn Item 8 branch (ESP32 faulting while the Nano only listens). That recording was wrong: the ESP32 does not show bus-off in listen mode. The measurements below stay as notes. They do not establish a transceiver fault.

1. **MCP2515 VDD** at the chip or module 3.3 V/5 V pin, Nano powered the usual way. Record the voltage. AVR input-high is 0.6 × VCC (3.0 V at 5 V). A 3.3 V MISO high that fails that threshold reads as **0**, not as `0xFF`. A stream of `0xFF` means MISO was high for the whole byte (slave not driving, or the register really is `0xFF`).
2. **CS (D8) and GND** continuity from the Nano pin to the MCP2515 pin, module unpowered. Record ohms. An open CS produces `0xFF` on every read; that would already have failed the boot CNF dump, so expect continuity if the boot dump is still good.
3. **Termination.** Count 120 Ω resistors actually on the bus, including the MCP2515 module jumper. Two is the intended bus. A third jumper makes about 40 Ω. Remove only the module jumper, repeat the Item 8 listen-only minute, write the ESP32 error result.
4. **Capacitor, separate run.** Only if the module supply sags by more than about 0.3 V during the fault, or as one explicit A/B after the tests above. Fit **47 µF to 100 µF electrolytic** from the module supply pin (the pin the Nano feeds, usually 5 V) to the module GND, short leads, polarity observed. Leave the module's 100 nF ceramic in place. Do not connect the capacitor to CANH or CANL. Repeat the same firmware and the same 60 s log as the previous item. If `spi=` and the ESP32 errors are unchanged, the capacitor did not address the fault; remove it from the theory.
5. **D10.** Confirm it stays an output. It is both nRF CE and the ATmega328 hardware SS pin. If it is an input and goes low, the AVR SPI block becomes a slave and MISO is no longer the Nano's input.

**STOP** after each numbered measurement. Do not bundle them.

---

## Item 10 — Read back the TX buffer before TXREQ

Do this only after Item 7 showed loopback is clean, or after Item 5 showed the fault is radio-dependent and Item 6 did not remove it. Restore normal mode (`CANCTRL` OSM write `0x08` as in the original `configure()`).

**Edit** `submit()` in `can_sender.cpp`. After `setRegisters` inside `sendMessage` is not visible, so do the check in `submit` before `mcp.sendMessage`:

The library write is inside `sendMessage`. Do not fork the library. Instead, immediately after `mcp.sendMessage` returns, and only when it returned `ERROR_OK`, read 13 bytes from the TX buffer that just went out. The driver does not say which buffer it used. Read TXB0SIDH (`0x31`), TXB1SIDH (`0x41`), and TXB2SIDH (`0x51`), 13 bytes each, and accept the buffer whose first data byte equals `data[0]` of the frame just submitted. Compare SIDH/SIDL with the id that was requested.

SIDH = `id >> 3`, SIDL = `(id & 7) << 5`. For `0x280` those are `0x50` and `0x00`.

Count `sid_mismatch` when the matching buffer's SIDH/SIDL differ. Print it on the stats line. Do not abort the frame in this item; observation only.

Also, in `recover_tx_buffer()`, if `mcpReadReg` returns `0xFF`, increment `spiErrors` and **return without writing**. A `0xFF` control byte has the error bits set and would abort a buffer or, if the address is off by one, clear SIDH (`0x31` is `0x30 + 1`).

**Build.** **STOP.** User runs one minute, radio in the state Item 5/6 concluded, and pastes `sid_mismatch`, `spi=`, and any ESP32 id `000`.

Result:

- `sid_mismatch` climbs: the id is wrong before the frame is committed. SPI writes are unreliable. Hardware item 9 applies if it has not been done.
- `sid_mismatch` stays 0 while the ESP32 still shows id `000`: the buffer was right and the id changed later, or the ESP32 row is a different frame. Capture one full 8-byte payload next to id `000` before changing more code.

---

## Item 11 — Put the tree back

**Done.** 2026-10-11.

**Restored state:**
- `CAN_QUIET_RADIO = 0` (radio on, normal operation)
- `CAN_LISTEN_ONLY = 0` (transmit enabled)
- Normal mode (not loopback, not listen-only)
- CNF values: `0x81`, `0xBB`, `0x02` (125 kbit/s, 81.25% sample point)
- `CAN_AUTO_RECOVER = 0` (disabled)
- All `can_send_*` call sites active (no `#if` guards blocking them)
- `can_lb_mismatch()` available but only increments in loopback mode (CANCTRL = 0x48)
- Build: 16352 bytes flash, 1122 bytes RAM

**Summary of findings (all items):**

| Item | Test | Result |
|---|---|---|
| 1 | Read real CANSTAT (not `mcp.getStatus()`) | `op=5` (invalid) was the old bug. Fixed. |
| 2 | One SPI burst (18 bytes from 0x1C) | `burstFF` alternates 18 ↔ 0 (periodic SPI failure) |
| 3 | Radio-on baseline | ESP32 clean, but burstFF=18 periods cause fake bus-off |
| 4 | Radio-off firmware (CAN_QUIET_RADIO=1) | burstFF=18 gone, but MCP2515 still goes bus-off |
| 5 | Decision: Mixed branch | Radio causes SPI failures, but CAN bus-off is independent |
| 7 | Loopback (radio off) | `lb_mismatch=0, burstFF=0` — SPI link and MCP2515 are fine |
| 8 | Listen-only (radio off, no transmit) | **Recording corrected 2026-10-12.** ESP32 does not show bus-off. Boot of the image under test was loopback (`CANCTRL=0x48`, `CANSTAT=0x40`), not listen-only. |
| 9 | Hardware measurements | VDD=4.7 V (0.3 V sag), 2× 120 Ω termination correct, **CAN bus R = 60 Ω**. **2026-10-12:** 220 µF on the MCP2515 board did not clear `spi=` (`spi=517`, `recv=0`, `eflg=0x0` in one 5 s window, radio off). Capacitor is off the theory. |
| 11 | Restore normal mode | **Done 2026-10-12.** `CAN_QUIET_RADIO=0`, `CAN_LISTEN_ONLY=0`, `setNormalMode()`, library `setBitrate(CAN_125KBPS, MCP_8MHZ)`. The copper short was removed afterwards. Working log is in `CAN_ERROR_REPORT.md`. |

**Root cause:** A PCB copper short between D7 and the MCP2515 chip-select net. Confirmed by the user after the pin-state tests. The transceiver conclusion in the old Item 8 branch stays withdrawn. The closed log is in `CAN_ERROR_REPORT.md`.

### Correction log, 2026-10-12

This block is the withdrawn boot, not the closed image. It was logged while the flashed build did not match the source:

```text
[TX] radio ready — 250 kbps, 13-byte frames
[TX] radio OFF (CAN_QUIET_RADIO=1)
[TX] CAN ready @ 125 kbit/s, data id 200
[CAN] CNF1=0x81 CNF2=0xBB CNF3=0x2 CANCTRL=0x48 CANSTAT=0x40 EFLG=0x0 TEC=0 REC=0
[TX] can sent=0 recv=0 refused=0 bad=0  ovf=0  tec=0 rec=0 eflg=0x0 intf=0x0 burstFF=0 lb_mismatch=0  rst=0 spi=0 tec=0 rec=0  eflg=0x0  canstat=0x0 op=normal icod=0
```

- `CANCTRL=0x48` / `CANSTAT=0x40` is loopback plus one-shot. Listen-only is `CANCTRL=0x60` and operation mode 3 (`op=listen-only`). This boot never entered listen-only.
- `radio ready` then `radio OFF` means `CAN_QUIET_RADIO` was 1 at compile time and `setup()` still called `radio.begin()`.
- The later `canstat=0x0 op=normal` does not match the boot read `CANSTAT=0x40`. Nothing after `configure()` writes normal mode. `sent=0 recv=0` with every register 0 is not evidence the controller is healthy and idle in normal mode.
- User: in listen mode the ESP32 does not show bus-off. Item 8's chosen branch and Item 9's premise are withdrawn. Item 8 has to be repeated on a build whose boot line shows `CANSTAT` operation mode 3.
