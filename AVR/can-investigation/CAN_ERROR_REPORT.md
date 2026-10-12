# CAN bus error report — Nano/MCP2515 corrupts a working bus

Date: 2026-10-11. Closed 2026-10-12.

## Resolution

A PCB copper short tied the D7 driver into the MCP2515 chip-select net. D7 (PD7) is the pad next to D8 (PB0). Making D7 an output, even held low with the LED off and the compare interrupts disabled, made the SPI probe fail. With D7 left as an input, the same timer and the same interrupts left the probe matching. After the short was removed, the stats line was clean:

```text
[TX] can sent=61 recv=20 refused=0 bad=0  ovf=0  tec=0 rec=0 eflg=0x0 intf=0x4  rst=0 spi=0 probe=0
```

`intf=0x4` is TX0IF. Before the short was found, a low or toggling D7 made later SPI bytes continue an old command: the address pointer walked the TX buffer and the status line printed probe-pattern bytes as `tec` / `rec` / `intf`. Other windows came back all `00` or all `FF`.

The timer itself is not the fault. With Timer1 in mode 14, both compare interrupts running, and `PORT_BIT` set to 0 so the interrupts do not write `PORTD`, SPI stays clean and the edge counters still climb (`edges=15708/15709`, `TIMSK1=0x06`). `TCCR1A` is 0, so OC1A/OC1B stay disconnected and the timer does not drive D9 or D10. The log line was `t1 A=0`.

Radio and CAN both work in that configuration. `CAN_QUIET_RADIO` and `CAN_LISTEN_ONLY` are 0. Boot and a later stats line:

```text
[TX] radio ready — 250 kbps, 13-byte frames
[TX] CAN ready @ 125 kbit/s, data id 200
[CAN] library 8 MHz / 125 kbit/s  mode=normal  EFLG=0x0 INTF=0x0 TEC=0 REC=0
[CAN] spi probe mismatch=0
[TX] t1 A=0 B=19 ICR1=16000 OCR1A=16000 OCR1B=4000 TCNT1=13554  timer on, D7 not driven
[TX] can sent=159 recv=110 refused=0 bad=0  ovf=0  tec=0 rec=0 eflg=0x0 intf=0x4  rst=0 spi=0 probe=0
```

`intf=0x4` on the earlier line is the same TX0IF flag. That sample was taken with D7 not driven, which avoided the short. The nRF24 path was confirmed working on that image. The post-repair sample above is the one with the copper removed.

Ruled out on the way:

- The 220 µF capacitor on the MCP2515 module. `spi=` kept climbing (`spi=517` in one 5 s window, radio off, listen-only).
- A permanently open D8 wire. Unplugging D8 makes the firmware report no controller. Putting it back restores init and a matching boot probe.
- The free-running ADC. One 5 s window failed while the converter was off and the next matched while it was on. With D7 not driven, the ADC stays on and `probe=0`.
- Hardware PWM on D9/D10. `COM1A` and `COM1B` were 00 whenever the timer was printed.

The early `recv` flood (tens of thousands) was a separate software bug: `readMessage()` with no buffer uses `READ_STATUS`, which reported a waiting frame while CANINTF was clear. The drain now counts a frame only after the receive flag is seen set and then seen clear. That fix is still in the tree. It did not stop the later `spi=` climb. Stopping the D7 edges did.

## Symptom

- Two ESP32 CYD CAN monitors (`../ESP32_projects/26_CYD_CAN_monitor`) exchange
  time on a 125 kbit/s bus and run error-free on their own.
- Adding the Nano (`01_heizung_temp`, ATmega328 + MCP2515) causes "CAN-Bus
  error"; the Nano receives a few frames then goes bus-off. Nano stats showed
  `recv`/`bad`/`ovf` in the thousands and later hundreds per second.
- ESP32 display showed phantom low IDs (`000`, `005`, `040`) and 8-byte frames
  truncated to 7 bytes.

## Topology

- Nano `01_heizung_temp`, rf-nano V1.0; MCP2515 on hardware SPI (CS = D8,
  INT = D2), 8 MHz crystal. Onboard nRF24 (CE = D10, CSN = D9) shares
  D11/D12/D13.
- 2x ESP32 CYD `26_CYD_CAN_monitor`, TWAI 125 kbit/s, node ID = last octet of
  WiFi IP. 2x 120 Ohm termination.

Serial ports observed: `83310` = ESP32 CAN monitor, `83320` = `02_nrf_receiver`,
`83330` = Nano, `83430`.

## Root cause

D7 edges coupling into MCP2515 chip-select. See Resolution. The paragraph that used to stand here called it a persistent shared-SPI fault and treated the nRF24 bus as the cause. The working image runs the nRF24 and the MCP2515 on that bus together, with `spi=0` and `probe=0`.

Three symptoms that showed up while D7 was toggling:

1. Phantom RX frames from a `READ_STATUS` path that counted a frame the flag had not confirmed. Fixed in the drain. Separate from the D7 fault.
2. Status fields that were probe-pattern bytes (`tec=195` is `0xC3`, `rec=129` is `0x81`, `intf=0x34`), three apart, which is one library register read apiece while chip-select stayed low.
3. `spi=` counting `0xFF` reads (MISO left high) and probe reads of thirteen `00` or thirteen `FF`.

`tec=0 rec=0` on a clean window is a real idle error state. The same numbers on a line whose probe is thirteen `FF` are just bytes that came back zero while another transfer failed.

## Evidence

- Boot dump reads the configured timing back correctly and counters at zero:
  `CNF1=0x81 CNF2=0xBB CNF3=0x2 CANCTRL=0x8 ... TEC=0 REC=0`.
- The `spi=` counter climbs at **both** 4 MHz and 1 MHz (approx. 70/s then
  30/s), so it is not a clock-speed problem.
- Phantom ID with real payload: `000: FA 00 E8 03 01 00 00` decodes as
  duty = 0x00FA = 250, hz = 0x03E8 = 1000, flags = 1 — the Nano's PWM report
  (`0x280`, `can_send_pwm(250, 1000, active)`) with a corrupted ID.

## Fixes applied

### AVR `01_heizung_temp`

- Bit timing matched to the ESP32's TWAI 80 %, SJW 3:
  `src/can_sender.cpp:38-40` (CNF values), `:139-149` (`setBitTiming()`),
  `:156` (`configure()`).
- Discard `0xFF` (failed) SPI reads instead of treating as bus-off:
  `src/can_sender.cpp:315`.
- Throttle `rx_drain()` to 1 ms and cap at 4 reads, so a glitch cannot become a
  flood: `src/can_sender.cpp:274-282`.
- Bus-off auto-recovery disabled during diagnosis:
  `src/can_sender.cpp:100`, call site `:349`.
- MCP2515 SPI clock lowered 8 -> 4 -> 1 MHz:
  `src/can_sender.cpp:53`, constructor `:109`.
- Diagnostics: `can_error_counters()` `src/can_sender.cpp:224`,
  `can_dump_config()` `:232`; stats line `src/main.cpp:358-362`,
  boot dump `src/main.cpp:437`.

### ESP32 `26_CYD_CAN_monitor`

- `format_data_bytes()` off-by-one that dropped the 8th byte:
  `src/main.cpp:333-340`, caller buffer enlarged `:471`.

## Hardware change, 2026-10-12

A **220 µF** capacitor was added on the MCP2515 board (the "2512" module). The investigation note had suggested 47 µF to 100 µF on the module supply pin to module GND; the fitted part is 220 µF.

It did not clear the fault. Radio off, listen-only, after the capacitor:

```text
[TX] radio OFF (CAN_QUIET_RADIO=1)
[TX] CAN ready @ 125 kbit/s, data id 200
[TX] can sent=0 recv=0 refused=0 bad=0  ovf=1  tec=0 rec=0 eflg=0x0 intf=0x0  rst=0 spi=517
```

Init still succeeds. `recv` stays 0. `spi=517` in one 5 s window is about 100 reads per second that came back `0xFF`. The printed `eflg=0x0` is a different read from those failures. The capacitor is off the theory.

## Open / remaining

- The copper short is removed. D7 may be driven again. `PWM_DUTY_PERMILLE` is 0, so boot holds the pin low until a CAN command. The post-repair stats line is `probe=0` `spi=0`.
- `CAN_AUTO_RECOVER` is 0. The working window did not need it.
- Bitrate is the library 8 MHz / 125 kbit/s table (`setBitrate(CAN_125KBPS, MCP_8MHZ)`), not the earlier custom CNF `0x81/0xBB/0x02`. The working stats line shows `tec=0 rec=0` while this node is transmitting.

## Diagnostic counters (`[TX] can ...` line, `src/main.cpp:341-377`)

| field | meaning |
|---|---|
| `sent`/`recv`/`refused` | frames this board sent / received / could not send |
| `bad` | frames rejected on the malformed-DLC path (`can_rx_malformed()`) |
| `ovf` | report windows with a receive-buffer overrun (`can_rx_overflows()`) |
| `rst` | successful automatic recoveries (`can_recoveries()`) |
| `spi` | `CANINTF` / `EFLG` reads that returned `0xFF` (`can_spi_errors()`) |
| `probe` | bytes that differed in the 13-byte TX-buffer write/read (`can_spi_probe()`). `0` is a match. A non-zero count is followed by the bytes read |
| `tec`/`rec` | MCP2515 transmit/receive error counters, each its own SPI read |
| `eflg` | MCP2515 EFLG, one SPI read. Warning names are printed only when the counter on that same line meets the datasheet threshold |
