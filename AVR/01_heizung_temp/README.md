# 01_heizung_temp — DS18B20 transmitter

Reads up to four DS18B20 sensors on a single 1-Wire bus and publishes them two
ways at once: over an nRF24 radio link, and on a CAN bus through an MCP2515.
The board also drives one PWM output and samples one analog input.

Runs on an ATmega328p (rf-nano V1.0) — 32 KB flash, 2 KB RAM.

## Pin assignment

The rf-nano exposes 20 pins and almost all of them are spoken for. D9 and D10
in particular look like the obvious choice for PWM because they are Timer1's
`OC1A`/`OC1B` — they are the nRF24's CSN and CE and must not be reassigned.

| Pin | Function | Dir | Notes |
|-----|----------|-----|-------|
| D0 / D1 | USB serial | — | 115200 baud |
| D2 | MCP2515 INT | IN | optional, currently unused |
| **D3** | *free* | — | Timer2 `OC2B` — spare 8-bit hardware PWM |
| D4 | 1-Wire data | bidir | DS18B20 bus; 4.7 kΩ pull-up to VCC |
| *D5* | *avoid* | — | Timer0 `OC0B` |
| *D6* | *avoid* | — | Timer0 `OC0A` |
| **D7** | **digital output** | OUT | software PWM from Timer1, 1 kHz. Boot duty is 0 until a CAN command |
| D8 | MCP2515 CSN | OUT | |
| D9 | nRF24 CSN | OUT | |
| D10 | nRF24 CE | OUT | |
| D11 | SPI MOSI | OUT | shared nRF24 + MCP2515 |
| D12 | SPI MISO | IN | shared nRF24 + MCP2515 |
| D13 | SPI SCK | OUT | shared nRF24 + MCP2515 |
| **AREF** | **must stay undriven** | — | internal 1.1 V bandgap is in use — see below |
| **A0** | **analog input** | IN | current coil, 50 Hz AC |
| A1–A5 | *free* | — | A6/A7 on the Nano are digital-only |

**Do not use D5 or D6.** They are Timer0, and `analogWrite()` on either one
rewrites Timer0's prescaler. `millis()` and `micros()` are Timer0 — so those pins
would silently degrade the 1-Wire timing and the nRF24 retry/timeout logic.

D3, D5, D6 and D7 are the only unassigned digital pins. D3 is kept free so that
8-bit *hardware* PWM remains available as a fallback if the CPU cost of software
PWM ever becomes a problem.

### Shared SPI bus

The nRF24 and the MCP2515 sit on the same hardware SPI bus and are told apart by
chip select (D9 and D8). Both drivers wrap every transfer in `SPISettings`
transactions, so arbitration is handled; the two devices are never addressed
simultaneously.

### MCP2515 configuration

- Bitrate **125 kbit/s**, selected by `CAN_BITRATE` in `src/can_sender.cpp`.
- The crystal is **8 MHz** and is declared explicitly as `MCP_8MHZ`. This is not
  a formality — see below.
- With nothing connected to the bus, no node ACKs, so frames are dropped and the
  `refused=` counter climbs. That is the expected behaviour on an unpopulated
  bus, not a fault — though `TXREQ` will sit set, because a frame that is never
  acknowledged is retried indefinitely. That state is reported explicitly rather
  than left as a silent bit; see below.

### Why the driver is `autowp/autowp-mcp2515`

It replaced `witchcraftworks/MCP2515-nb`, whose `getCnfForClockFrequency8e6()` and
`getCnfForClockFrequency16e6()` return **identical** register values. With those
registers, time quantum is `2 × (BRP+1) / Fosc`, so an 8 MHz part driven with 16 MHz
timing makes every quantum twice as long and the controller transmits at **half**
the intended rate — 62.5 kbit/s on a 125 kbit/s bus. Nothing reports that: the
frame is simply never acknowledged.

`autowp` takes the oscillator frequency as an argument (`setBitrate(CAN_125KBPS,
MCP_8MHZ)`) and has genuinely distinct per-clock tables. It is the same library
and the same call the working `haus/stromzaehler` project uses on this hardware.

Two behaviours it changes here, both improvements:

- **Transmit never blocks.** `sendMessage(frame)` scans the three TX buffers and
  returns `ERROR_ALLTXBUSY` at once if all are busy, so the caller drops the frame
  rather than waiting. The old driver's blocking path polled `TXBnCTRL` with no
  timeout while auto-retry was enabled, which on a bus where nothing ACKs never
  exits and would hang the temperature and radio paths with it.
- **No software TX queue.** The packet pool and its `_started` reconstruction
  hack — needed only because `MCP2515-nb` never clears that flag, making a
  `CANPacket` transmit exactly once ever — are both gone. Worth 139 B of RAM.

### Bus health over nRF

The `0xABD2` radio frame and CAN `0x300` carry the same thing, so the bus can be
watched from a receiver with no CAN sniffer attached:

```
[0..1] sent      frames accepted for transmission since boot
[2..3] received  frames taken off the bus since boot, before any filtering
[4..5] refused   frames this board would not transmit (queue full, no controller)
[6]    EFLG      MCP2515 error flags, as read
[7]    CANSTAT   MCP2515 controller status, as read
```

The radio frame carries a tenth field, `present` (`STATS_FLAG_PRESENT`): whether
an MCP2515 answered on SPI and `can_init()` succeeded. **This is the bit that
separates "the bus is broken" from "there is no bus".** With the controller
absent, `can_sender` is deliberately fail-soft — every send is a no-op, the
radio and temperature paths keep running — which is right for an unpopulated board
but leaves every other counter reading zero. Those are the same numbers a
perfectly healthy idle bus produces, and a wireless observer cannot tell the two
apart without this flag. Reported last on the serial line, because it subsumes
everything else it would otherwise qualify.

The CAN copy has no DLC-8 room for it, so it rides in bit 7 of the `CANSTAT`
byte (`0x80`). `EFLG` is a 5-bit field and `CANSTAT` 4-bit, so the bit is spare in
both and the packing is lossless.

`received` counts before the PWM filter, deliberately: a device that starts
sending garbage should show as a rising count, not as silence.

`refused` is the same value as the old `can_drop=` counter and means the driver's
TX queue was full. It rises rather than back-pressuring the caller on purpose —
this board also owns the temperature and radio paths, and those must not stall
because CAN is busy.

**EFLG is read-to-clear**, so it is sampled once per report window rather than in
`can_pump()`. Polling it every pass would erase the fault bits between reports. A
fault therefore appears in the window it happened in and then clears, which is
also why the rate is 5 s and not 1 s — a shorter window mostly buys a shorter
period in which a transient stays visible.

Named bits, so the serial line is actionable without register knowledge:

| EFLG | | Meaning |
|---|---|---|
| `0x01` | EWARN | any error counter 96–127 |
| `0x02` | RXWAR | receive error warning, REC 96–127 |
| `0x04` | TXWAR | transmit error warning, TEC 96–127 |
| `0x08` | RXEP | receive error passive, REC ≥ 128 |
| `0x10` | TXEP | transmit error passive, TEC ≥ 128 |
| `0x20` | TXBO | **transmit bus-off** — the node has left the bus |
| `0x40` | RX0OVR | receive buffer 0 overflow, frames lost |
| `0x80` | RX1OVR | receive buffer 1 overflow, frames lost |

**Do not decode these with ATmega8/16/32 bit names.** The classic AVR on-chip CAN
controller has `EFAULT`, `ENRXERR`, `EBUFOVFL` and `ERXOVFL` in bits 1–4; the
MCP2515 has entirely different flags in those same positions. An earlier revision
of this file made exactly that mistake, and it inverted the diagnosis: `0x55` was
reported as receive errors and receive overflow when the MCP2515 actually said
**`TXEP + TXWAR + EWARN + RX0OVR`** — a pure transmit-side fault, which is the
signature of a bitrate mismatch, with no receive error at all.

| CANSTAT | | Meaning |
|---|---|---|
| `0x04` | TXREQ | a frame has not been ACKed and is still being retried |
| `0x08` | RXERR | receive error |
| `0x10` | TXERR | transmit error |
| `0x20` | TXWARN | transmit error warning |

**`TXREQ` set on an otherwise idle bus is normal here** — nothing is connected to
ACK, so the frame retries forever. "Transmitting fine" and "transmitting into a
void" are indistinguishable from the counters alone, which is why this is called
out rather than left as a hex byte.

**TEC and REC are not reported in the stats frame, but the driver can now read
them.** `autowp` exposes `errorCountTX()` and `errorCountRX()`, which the previous
driver could not reach without patching. They are not in the frame because the 8
data bytes are already full, but they are what would confirm a bitrate mismatch
directly, and are worth adding if this needs proving rather than inferring.

## Radio protocol

All frame types share one nRF24 pipe and are told apart by their magic byte.
They are **13 bytes** each.

| | Magic | Payload | Cadence |
|---|---|---|---|
| Temperature | `0xABCD` | 4 × centidegree + presence bitmask | ~5.8 s |
| Identity | `0xABCE` | rank + full 8-byte DS18B20 ROM | burst at boot, then 60 s |
| PWM state | `0xABCF` | duty, frequency, flags | on change, then ~5.8 s |
| ADC window | `0xABD1` | min, max, peak-to-peak, reading count, flags | 1 s |
| CAN health | `0xABD2` | sent, received, refused, EFLG, CANSTAT | 5 s |

**Why identity is a separate frame.** Four full ROMs need 32 bytes before any
framing, which is exactly the nRF24L01+ payload ceiling, so they cannot travel
alongside the readings. They are also static, so re-sending them every second
would buy nothing. Announcing them cyclically means a receiver that powers up at
any moment converges on the full sensor set with no handshake and no ordering
requirement — it simply waits for the announcements it has not seen yet.

**Rank** is assigned by sorting the currently-present sensors by ROM address,
and is what maps onto the CAN identifier. Note that rank therefore shifts if a
sensor is unplugged; the receiver detects and reports this rather than silently
substituting a different sensor.

Two pipes were tried first and cannot work through this library: only the first
five bytes of an address array reach the radio, so a sixth "pipe selector" byte
is discarded and both pipes collapse onto one address.

## CAN mapping

Classic CAN carries at most 8 data bytes — exactly one DS18B20 ROM, and exactly
four centidegree readings.

| Identifier | DLC | Direction | Payload |
|---|---|---|---|
| `0x180 + rank` | 8 | out | full DS18B20 ROM |
| `0x200` | 8 | out | four `int16_t` centidegrees |
| `0x240` | 8 | **in** | PWM duty command |
| `0x280` | 8 | out | PWM duty report |
| `0x2C0` | 8 | out | ADC window: max, min, peak-to-peak, flags |
| `0x300` | 8 | out | CAN health: sent, received, refused, EFLG, CANSTAT |

Both PWM identifiers use the same 8-byte layout:

```
[0..1]  duty in permille, uint16 little-endian   (0…1000)
[2..3]  frequency in Hz, uint16 little-endian
[4]     flags — bit 0 = active while 0 < duty < 1000 %
[5..7]  reserved
```

Slots with no sensor carry `-32768` (`ERR_FLAG`). No software CRC is added: CAN
already checks every frame with hardware CRC-16 and bit stuffing.

## Digital output (D7)

1 kHz, adjustable duty, ~14-bit resolution in 1 µs steps.

The rf-nano is a black-box module — D7 is one of its pins, and the schematic
shows only the external circuit connected to that pin, not the nano's internals.

A PCB copper short tied this net to D8, the MCP2515 chip-select. With the short
in place, driving D7 low made the SPI probe fail even when the compare
interrupts were off and the LED current was zero. Leaving D7 as an input
avoided the short. The copper has been removed. After that, a stats line read
`sent=61 recv=20 tec=0 rec=0 eflg=0x0 intf=0x4 spi=0 probe=0`.

```cpp
pwm_out_set(250);   // 25 %
pwm_out_set(0);     // exact 0 %,  interrupts off, pin driven low
pwm_out_set(1000);  // exact 100 %, interrupts off, pin driven high
```

### Commanding it

The duty is commanded from CAN (`0x240`) or from the radio (`0xABD0`), and it
is **also durable**: a node with no live command at all comes back on whatever was
last in force. Applied verbatim, last writer wins. There is deliberately no
ramping, no minimum on/off pulse and no validation beyond the permille clamp.

There is deliberately **no dead-man timeout**. The duty is durable state, not a
lease: expiring it would drop the heat every time the controller above this node
rebooted. The upstream device is the thing with a timeout; this node's job is to
honour the last instruction until it is given another one. See `doc/risk_analysis.md`
§3.0 for the reasoning.

Every duty is attributed to its source, and the source is reported on both wires
(`PWM_SRC_` in the `0x2C8` flags and in the PWM frame) — so a receiver can tell
"nobody is commanding this node" apart from "somebody is commanding it to OFF",
which are the same duty and completely different situations:

| Source | Meaning |
|---|---|
| `PWM_SRC_EEPROM` | restored from the log; no live command received yet |
| `PWM_SRC_CAN` | last valid command was CAN `0x240` |
| `PWM_SRC_NRF` | last valid command was radio `0xABD0` |
| `PWM_SRC_FAULT` | local safety action, e.g. a PWM ISR stall |

The current duty is then mirrored outward on both buses: on `0x280` and on the
`0xABCF` radio frame. It is repeated once per measurement cycle as well as on
every change, so a receiver that powers up late converges on the correct value
without a request/response exchange.

Boot duty comes from the EEPROM restore-point log (see below), so an
unattended winter reboot resumes the duty in force rather than dropping to OFF.

### The PWM output path, and what happens when it dies

The duty is applied by Timer1 compare interrupts. If those stop matching — a
stalled ISR, a wedged timer — the commanded duty stops reaching the load
silently, which for a heating load in February is the failure that matters. That
is handled as a three-step escalation, documented in `doc/risk_analysis.md` §1.2:

1. **First stall** — record the fault in the EEPROM log, let the watchdog reboot
   the node. A reboot re-establishes the timer from a known-good register state.
2. **Fault already in the log at boot** — enter *safe mode*: a 1 kHz waveform at
   **200 permille**, driven directly on the pin and timed off `micros()`, because
   if the compare vectors are dead there is no way to make edges with them. 200
   permille is the measured ~7 V point from the table below — enough to keep the
   pipe and the electronics alive, not enough to overheat the element.
3. **Recovery** — the bench command `V` (`SYSCMD_REVIVE`) hands the pin back to
   Timer1. If the path is genuinely broken, the liveness monitor puts it straight
   back into safe mode, so the attempt is bounded rather than being a way to
   escape a real fault.

Safe mode is not a terminal state. It is advanced from `loop()`, so CAN, the ADC,
the watchdog and the serial console all keep running. On the wire the source
reads `PWM_SRC_FAULT` for its whole duration rather than the node looking
merely idle.

Two details that are easy to get wrong and were both wrong here first:

- **`PORF` is not enough to trust `.noinit`.** The fault latch lives in a region
  that survives a watchdog reset, but `avrdude` **also** preserves `.noinit`
  across a flash upload. A freshly flashed node with a never-written latch would
  read as a fault and go straight into safe mode on a perfectly healthy output.
  The latch is now magic-guarded (`PWM_BOOT_MAGIC`) and ignored unless it is
  genuinely ours.
- **The requested duty is not the delivered duty.** The waveform can only change
  state at a `loop()` pass boundary, so the delivered high time is quantised up.
  Rising/falling edge counters report what actually happened on the pin rather
  than assuming the request was honoured — the log prints `delivered (n…)`.

Because the fault is written to the EEPROM ring, it **survives reflashing**. That
is the point of recording it, but it also means a flash is not a clean slate: a
node may come up reporting `PWM-OUTPUT-FAILED` from a fault that has long since
been fixed. The receiver ages the flag for this reason — neither side has a clock,
so it reports how long *it* has been observing it. `X` at the receiver erases the
log.

### How the radio commands it, despite being half-duplex

The nRF24 is half-duplex and this board is primarily a transmitter, so it cannot
listen continuously without interleaving with the temperature broadcasts. Instead
each measurement cycle opens one short **listen window** (250 ms) between
broadcasts, drains at most one command, and returns to transmitting. The command
pipe is `0xABD0`, distinct from the `0xABD1` broadcast pipe, so commands and
broadcasts are separate addresses on the same radio.

The window is kept deliberately short, and `CAN` polling, the ADC sampling and
the PWM liveness monitor all keep running *inside* it. Two reasons: a 250 ms
blind spot is 25 windows of a 50 Hz signal — enough to swallow whole half-cycles
in the min/max statistic — and the window is exactly as long as
`PWM_STALL_LIMIT_MS`, so keeping the stall check inside it means the stall timer
cannot be fooled by the block. The watch is fed on the way through.

**Known exposure:** the RX pipe number is not checked before accepting a command,
so a `0xABD0` frame delivered to the *broadcast* address would be honoured by every
node that heard it. Tracked as §4.4 in `doc/risk_analysis.md`; the fix is a
`getLastRxPipe()` check before the duty is applied.

The endpoints bypass the counter entirely: there are no edges to generate, so
both interrupts are disabled and the port is driven directly. Interior values
(1…999 permille) re-enable the interrupt pair.

This matters more than it looks. Setting the duty register equal to `TOP` would make the
counter match *and* overflow in the same cycle, and overflow wins the priority
contest (`OVF1` is vector 5, `OCF1A` is vector 11) — the pin would go high and
then straight back low, i.e. nearly 0 % rather than 100 %. Likewise a zero duty
emitted a ~1 µs spike on every period. Both endpoints now avoid the counter.

Duty is written to `OCR1B`, which is backed by an atomic 16-bit temp register,
so it can be changed from the main loop at any time without masking interrupts.

D4 and D7 share port D, and the ISRs use read-modify-write on `PORTD`. That is
safe only because OneWire performs its bit-banging writes inside `cli()`/`sei()`
critical sections — a non-atomic `PORTD` write added elsewhere in the main loop
would race the PWM edge.

### Measured output

Measured 2026-10-11 by commanding a duty from the receiver's serial console and
probing the node with a DC voltmeter.

**The drive polarity is still unverified at the load.** Everything above assumes
it is *inverting*: commanded 0 % reads 11.57 V and commanded 100 % reads 0.137 V.
If that is wrong at the load, the safe-mode duty of 200 permille is wrong with it.
Confirming this is item B1.2 in `doc/risk_analysis.md` §7 and is the one thing
still blocking unattended winter operation.

| Commanded duty | Measured | Implied inverted duty * |
|---|---|---|
| 0 % | 11.57 V | 0 % |
| 1 % | 11.04 V | 4.6 % |
| 2 % | 10.63 V | 8.2 % |
| 5 % | 9.80 V | 15.5 % |
| 10 % | 8.75 V | 24.7 % |
| 20 % | 6.99 V | 40.1 % |
| 40 % | 4.365 V | 63.0 % |
| 60 % | 2.491 V | 79.4 % |
| 80 % | 1.087 V | 91.7 % |
| 100 % | 0.137 V | 100 % |

\* `1 − (11.57 − V) / (11.57 − 0.137)`, i.e. the fraction of the swing that the
probe reads as high, using the two measured endpoints as the reference.

**These are not D7 measurements.** 11.57 V exceeds the ATmega328p supply because
the probe sits on the downstream side of a galvanically isolated driver stage.
The rf-nano is a black-box module; D7 is one of its pins, and the schematic
shows only the external circuit connected to that pin — the nano's internals
(Timer1, software PWM, firmware) are not drawn.

The external circuit connected to D7:

| ref | value | part | role |
|---|---|---|---|
| R7 | 620 Ω | — | PC817 LED series, fed from D7 |
| U1 | — | PC817 | LED / phototransistor isolation |
| C1 | 22 µF | — | output node capacitor |
| R4 | 4.3 kΩ | — | in series with C1 |
| R5 | 3.3 kΩ | — | output RC series element |
| C2 | 4.7 µF | — | output RC capacitor, **probe point** |
| U2 | — | TL431LP | precision shunt regulator in the output path |
| R1 | 3.3 kΩ | — | **TL431 sense resistor** (sets sink current = 2.5 V / R1) |
| R3 | 3.3 kΩ | — | adjacent to U2 |
| R2 | 12 kΩ | — | adjacent to U2 |
| R6 | 470 Ω | — | near the PC817 output |
| J3 | — | Screw 1x03 | external 3-pin terminal |

LED current is `(5.0 − 1.2) / 620` ≈ **6.1 mA**, above the PC817's 5 mA test
condition. The external load has a **high-impedance input**, so it draws
negligible current from the output node — the node voltage is determined by
the division between the phototransistor's on-resistance and the TL431's
constant-current sink (`2.5 V / R1` ≈ 0.76 mA).

**The response is inverted** — more commanded duty gives *less* voltage —
which is what an inverting driver (common-emitter NPN, or N-channel MOSFET with
a pull-up) produces. The two endpoints confirm the switching itself is sound:
11.57 V at 0 % and 0.137 V at 100 % are close to rail-to-rail, so D7 is swinging
fully and duty is reaching the load.

**The mid-range is markedly non-linear.** If the probe saw the average of a clean
square wave through an inverting stage, 20 % duty would read 12 × (1 − 0.20) =
9.6 V; it reads 6.99 V. Note that this cannot be explained by filtering: any
linear filter preserves the mean, so an RC or similar stage cannot bend the
average at all. The curvature therefore comes from the circuit itself or from
how the measurement is being taken, not from smoothing.

The table above is directly usable as a calibration curve as it stands, but
control authority would be poor in the mid-range: to move the probe between
40 % and 60 % of its swing requires commanding 20 % to 40 % of duty.

**U2 is a TL431LP precision shunt regulator, not a plain pull-up resistor.**
This is the "current limit" in this stage, and it means the load current is set
by the TL431 reference against a sense resistor (R1) rather than by the rail
and a series resistor. Any analysis that treats the 12 V side as a resistive
pull-up is therefore wrong.

**Schematic note.** The KiCad file has no `Wire` objects connecting the output
circuit to the nano pin labels — 35 header pin stubs with labels on one side,
a separate island of 44 wires carrying the output circuit on the other, with
**no wire bridging the two**. The connection exists on the actual PCB (D7 → R7
→ PC817), but the netlist in the KiCad file is incomplete — no wires were
drawn. The nano itself (ATmega328p, firmware, Timer1) is a black-box module;
the schematic shows only the external circuit connected to its pins.

**The RC values, against a 1 ms PWM period:**

| network | τ = R·C | vs PWM period |
|---|---|---|
| 3.3 kΩ × 4.7 µF | 15.5 ms | 15.5 × |
| 4.3 kΩ × 22 µF | 94.6 ms | 94.6 × |

So the probe node responds 15–95 PWM periods too slowly to see any ripple. It
tracks the long-term average only. This is desirable for ripple rejection but
means roughly 15 ms (and up to 95 ms) of lag behind any commanded change, which
will dominate the response of whatever the load is.

**The RC cannot explain the non-linearity.** A linear filter preserves the mean
of its input, so any passive RC — asymmetric charge and discharge included —
would still read `V_hi·(1−d) + V_lo·d`. The curvature is therefore not a
filtering artefact and comes from the switching stage, not the smoothing.

With a TL431 sink in circuit, the most likely mechanism is the TL431 and the
phototransistor sharing the same node: both conduct, the node voltage sets how
hard each conducts, and the measured point moves with the division between them
rather than tracking duty directly. That also fits the 0.137 V floor appearing
only at 100 % duty, where there is continuous time to settle. Confirm against
the drawn netlist before treating this as settled.

PC817 CTR remains a distribution (roughly 50–600 %), varying with temperature
and age, so anything that depends on it being high will drift.

**Direction check:** the stage inverts, so more duty produces less output. If
the load is a heating valve or element where more duty should mean more heat,
the commanded duty is currently backwards and would need `(1000 − permille)`.

### Control formula for the usable range

A least-squares power-law fit over the measured points from 0 % to 60 % duty:

```
V(duty %) = 11.57 − 0.564 · duty^0.691          volts
duty (%)  = ((11.57 − V) / 0.564)^1.447         inverse
```

| duty | 0 % | 1 % | 2 % | 5 % | 10 % | 20 % | 40 % | 60 % |
|---|---|---|---|---|---|---|---|---|
| measured | 11.57 | 11.04 | 10.63 | 9.80 | 8.75 | 6.99 | 4.365 | 2.491 |
| formula | 11.57 | 11.01 | 10.66 | 9.86 | 8.81 | 7.11 | 4.37 | 2.04 |

Worst deviation is 0.12 V up to 40 % duty; it reaches 0.45 V at 60 %. For an
8-bit MCU, `pow()` is expensive in flash, so a small table with linear
interpolation is usually the cheaper route:

```c
// duty % → millivolts, 0…60 % in 10 % steps, linear interpolation between
static const uint16_t DUTY_MV[7] = { 11570, 8750, 6990, 5670, 4370, 3170, 2490 };
```

**Pump cutoff.** The pump stops below 2.5 V, which the formula puts at
**duty ≈ 56 %**. Clamp commanded duty to 55 % or lower; above that the pump is
off and further increases do nothing. The usable control span is therefore
0–55 %, and note that the response is steep near 0 % and flattens toward the
top, so mid-span steps of ~1 % duty buy little while the bottom few percent
matter a lot.

## Analog input (A0) — current coil, 50 Hz AC

A0 carries the secondary of a current-measurement coil. The board tracks the
**minimum and maximum** over each one-second window and publishes both plus the
peak-to-peak span, once a second, on CAN (`0x2C0`) and on the `0xABD1` radio
frame.

The CAN frame at `0x2C0` is DLC 8:

```
[0..1]  max, raw ADC counts
[2..3]  min, raw ADC counts
[4..5]  peak-to-peak, max - min
[6]     flags — bit 0 internal 1.1 V reference, bit 1 railed
[7]     reserved
```

The radio frame is the same values plus the reading count, which is why it is 13
bytes rather than 8:

```
magic(2) + max(2) + min(2) + pp(2) + readings(2) + flags(1) + crc(2)
```

`readings` is how many samples were folded into the window. It is a diagnostic for
the sampling path, not part of the measurement — `pp` does not depend on it. It
saturates at 65535 rather than wrapping.

### Why min/max and not an average

This is an **extremum** measurement. The quantity of interest is the envelope of
a sine wave; an average of that wave reports a number that tracks the load but
not the current amplitude, which discards precisely the part worth measuring.
Earlier revisions averaged 8 samples per log line, which was only ever meant to
make the serial output readable.

### One window is one second = 50 line periods

The window is 50 whole periods of 50 Hz, which makes `ADmax`/`ADmin` the true
envelope extremes.

It is worth being precise about how much of that actually matters, because it
bounds how carefully the timing has to be kept. Min and max are *per-cycle
stationary* statistics: any window appreciably longer than one 20 ms period
already contains complete peaks, so the reported extremes are correct regardless
of where the window edges land relative to the waveform. Loop jitter cannot
corrupt the result, and the 1 s boundary does not need to be aligned to a zero
crossing.

What 50 periods buys is confidence, not correctness — 50 independent chances to
catch a peak, so a single sampling dropout cannot pull the extreme in.

### Sample rate

The ADC free-runs at **9615 samples/s** (`/128` prescaler → 125 kHz ADC clock →
13 cycles → 104 µs per conversion). `/128` is the slowest clock that still lets
the sample capacitor settle against a high-impedance source within those 13
cycles, so it is the accuracy-optimal choice. For 50 Hz, Nyquist needs 100/s;
we are oversampling by ~96× so that a peak is not missed between reads.

The ADC is **never waited on**. Conversions complete on their own and the loop
reads whatever the last finished conversion was, so there is no 104 µs stall
anywhere in the loop. This is deliberate: 1 kHz PWM on D7 has a documented 1 µs
duty resolution, and a blocking conversion would inject ~10 % of a period of
jitter into the edge timing.

Sampling happens on every loop pass **and inside `listen_window()`**, which
matters more than it looks. `listen_window()` blocks for 250 ms; left unsampled
that would be 12.5 whole cycles of the input signal with no data at all, and
because min/max is an extremum statistic, a gap that long can swallow half-cycles
and pull the reported peak in.

### Reference voltage — internal 1.1 V bandgap

One LSB is 1.100 V / 1024 = **1074.22 µV**. Counts are transmitted **raw, not
scaled**: the transmitter cannot vouch for a bandgap's absolute accuracy, so the
conversion to volts, and any calibration against it, belongs downstream.

`adc_monitor_begin()` selects the internal reference with `REFS1:0 = 11`:

| REFS1:0 | Source |
|---|---|
| `00` | external voltage at the AREF pin (internal Vref turned off) |
| `01` | AVCC with an external capacitor at AREF — the reset default |
| `10` | reserved on this part |
| `11` | **internal 1.1 V bandgap** — what this board uses |

`REFS1:0 = 11` is selected per the ATmega328P datasheet, Section 23 (ADMUX),
Table 23-3: *"Internal 1.1V voltage reference with external capacitor at AREF
pin"*. One LSB is 1.100 V / 1024 = **1074.22 µV**.

**The 2.56 V figure belongs to a different chip.** It is real, but for the older
classic megaAVR family — ATmega8/16/32 — whose ADMUX table does say 2.56 V. Those
datasheets are frequently served for ATmega328P searches, and the register
descriptions are otherwise near-identical, so the 2.56 V text transfers very
plausibly and silently. Two independent confirmations for 1.1 V on *this* part:
the Table 23-3 text above, and the ADC block diagram in Microchip's ATmega328P
Silicon Errata and Data Sheet Clarifications (DS80001060), which is labelled
"INTERNAL 1.1V REFERENCE".

### AREF must be undriven in this mode

This is a hard datasheet restriction, not a style preference: *"The internal
voltage reference options may not be used if an external reference voltage is
being applied to the AREF pin."* The internal bandgap is a generated source, so
an external voltage on AREF would tie two sources against each other.

Consequences for the board:

- **AREF carries only its bypass capacitor to ground.** No reference IC, no
  divider, no external supply.
- Any external 1 V reference fitted to this design must be **removed** before
  running with `REFS1:0 = 11`.
- A Nano's `AVREF`/`AREF` header pin must be left unconnected. Note the rf-nano
  breaks `AREF` out at the header, which makes it easy to populate by mistake.

### Accuracy cost

The internal bandgap is specified loosely — roughly ±2 % typical over
temperature, drifting with VCC and temperature — against ±0.1 % for a discrete
reference such as a REF3040 or TLV431. For a current measurement that wants
absolute accuracy, this trades measurability for precision of the scale factor.

Two things partly compensate, and one does not:

- Counts stay **raw** on the wire, so a calibration can be applied later.
- A coil secondary is a ratio measurement, so a common-mode reference error
  partly cancels.
- **It does not cancel in calibration across temperature.** The bandgap drifts,
  so a single-point calibration ages badly.

Revisit this if absolute accuracy turns out to matter more than avoiding an
external part: `REFS1:0 = 00` plus a real 1 V reference returns LSB to
976.56 µV and takes the reference out of the temperature loop.

Two traps worth writing down:

**The macro names are misleading here, so the bits are written explicitly.**
`INTERNAL` is the right choice for this board, but its meaning is not obvious:
`EXTERNAL` (0) → the AREF pin, `DEFAULT` (1) → AVCC, `INTERNAL` (3) → the
on-chip 1.1 V bandgap. The names describe the *wire*, not the source — "internal"
here means on-chip, and has nothing to do with the 1 V reference that the external
option would supply. `adc_monitor.cpp` therefore sets `REFS1:0` directly.

The ADC's unipolar input is worth noting for an AC source: the negative
half-cycle is clamped at AVss, so `ADmin` sits at 0 for a signal centred on zero
and the useful envelope quantity is `ADmax` alone. A biasing network that lifts
the coil signal into the 0…1 V range preserves both halves.

## Build

```sh
pio run                 # compile
pio run -t upload       # flash
pio device monitor      # 115200 baud
```

Current footprint: **1054 B RAM (51 %), 14.9 kB flash (48 %).**

## Notes

- `heizung_system.md` in the repository root still lists the 1-Wire bus as
  **D12** in places. The wiring is **D4** — this README and the code are
  authoritative.
- Sensor ranking is recomputed from the bus every cycle, so unplugging a sensor
  removes it from the announcements on the next pass.