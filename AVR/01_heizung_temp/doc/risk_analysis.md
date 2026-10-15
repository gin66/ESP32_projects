# Risk analysis — 01_heizung_temp

Review date: 2026-10-14
Reviewed revision: `main.cpp` 619 lines, `can_sender.cpp` 406, `adc_monitor.cpp` 124, `pwm_out.cpp` 120
Build under review: DallasTemperature 4.0.6, OneWire 2.3.8, RF24 1.6.2, autowp-mcp2515 1.3.1
Footprint after remediation: 979 B RAM (47.8 %), 17 212 B flash (56.0 %) — was 1086 B / 16 024 B

Scope: whether this board can hang, loop forever, divide by zero, fault at runtime, or
otherwise fail in a way that leaves a house heating system uncontrolled. The design
intent is continuous 24/7 operation for a year.

**Status: five findings have been remediated in the firmware since this review was
first written — §1.1, §1.3, §1.4, §4.5 and the peripheral part of §3.0. See
[§7 Mitigation action plan](#7-mitigation-action-plan) for what remains and the
order it should be done in.**

**Headline: there is exactly one true infinite loop in the code, and it was on the
safety-critical boot path (§1.1 — now removed). Everything else was bounded — the
libraries are correctly chosen for that. But the dominant risk is not a hang at all:
it is that this board has no thermostat and no failsafe (§3). Its boot state, after
every power cut, is *heater off* — enforced by a hardcoded constant whose
justification is an inverting transfer function that has since been measured and
confirmed at the load (§3.0, B1.2).
In winter, that is a silently frozen house, and nothing in the firmware would notice
or age it out. That is now the single highest-priority open item.**

## Review status

| # | Risk | Severity | Status |
|---|------|----------|--------|
| 3.0 | **Freeze: power loss → boot duty is heater OFF** | **Critical** | **FIXED** — restore wired in, `PWM_SRC_*` reporting added |
| 1.6 | PWM output path dead → duty silently undelivered | High | **FIXED** — three-step escalation, documented in §1.2 |
| 1.7 | `.noinit` fault latch trusted across a flash → false failover on a healthy output | High | **FIXED** — magic-guarded, §1.2 |
| 3.1 | ~~No command timeout / dead-man switch~~ | — | **CLOSED by decision** — rejected, see §3.0 |
| 3.2 | No duty clamp, ramp or rate limit | High | OPEN |
| 4.1 | `DEVICE_FAULT_*` not mapped → −252…−254 °C as valid data | High | OPEN |
| 4.2 | DS18B20 CRC / sentinel collision at −55 °C | High | OPEN (partly inherent) |
| 4.3 | `readIntfStable` livelock starves CAN RX | High | OPEN |
| 1.2 | No watchdog | Critical | **FIXED** — §1.2 |
| 1.1 | `while(1){}` on radio init failure | Critical | **FIXED** — §1.1 |
| 1.3 | CAN TX lockout after 3 un-ACKed frames | Critical | **FIXED** — §1.3 |
| 1.4 | Bus-off detected, never recovered | High | **FIXED** — §1.4 |
| 2.1 | Radio write stalls 190 ms/frame when radio dead | Medium | Mitigated, bounded |
| 2.2 | 1-Wire re-enumerated every cycle (~10 ROM searches) | Medium | OPEN |
| 4.4 | PWM command injectable via broadcast pipe 0 | Medium | OPEN |
| 4.5 | `can_spi_probe()` corrupts an in-flight CAN frame every 5 s | Medium | **FIXED** (partially) |
| 5.1 | All health counters `uint16_t`, wrap in ≈2 h | Medium | OPEN |
| 5.2 | ISR edge counters read non-atomically | Low | **FIXED** — §1.2 |
| 6.1 | `millis()` rollover: one non-idiomatic compare | Low | OPEN |

---

## 1. Blocking faults and hangs

### 1.1 FIXED — `while (1) {}` on radio init failure

*(Line numbers as of the pre-remediation revision.)*

`src/main.cpp:460-463`

```cpp
if (!radio.begin()) {
    Serial.println("[TX] ERROR: nRF24 not responding!");
    while (1) {}
}
```

This is the only genuine unbounded loop in the firmware, and it sits on the boot
path of a mains-adjacent heating controller. Consequences:

* The node stops permanently. No watchdog (§1.2) exists to recover it. Recovery
  requires a human with a screwdriver.
* It happens **before** `pwm_out_begin()` (`main.cpp:489`), so D7 is still a
  high-impedance input. The output stage is left floating for the entire outage —
  the heater runs at whatever the driver stage does with an unconnected input,
  which is not the state the code intends and not necessarily the safe state.
* The failure mode is trivially reachable. RF24's `begin()` returns false purely
  from one SPI readback of CONFIG (`RF24.cpp:1127`), after a `delay(5)`. A brownout
  during the 5 ms, an inrush transient, or a marginal 3.3 V LDO on the rf-nano
  that sags during the nRF24's PA_LNA startup current is enough.

**This must not remain a hard hang.** The correct behaviour is to log, then carry
on with the temperature path (which needs no radio), and retry the radio
periodically — the same fail-soft stance the CAN driver already takes.

**What was implemented.** Both peripherals were moved out of `setup()` into a
state machine in `loop()` (`peripherials_tick()` in `main.cpp`).

* `setup()` now contains **nothing that can fail or block** before the PWM output
  is pinned. The order is: CSN lines high → `pwm_out_begin()` →
  `set_pwm_duty()` → `Serial.begin()` → sensors. This matters independently of the
  hang — `sensors.begin()` alone can burn ~200 ms in ROM-search retries, and it
  used to run *after* the radio check, so a dead radio meant D7 stayed
  high-impedance far longer than necessary. A floating driver input is an
  undefined output, not a safe one.
* Radio: attempts `radio_bring_up()` every 5 s until it succeeds. On success it
  logs once and resets `idPeriodMs` to the burst value so a late-coming radio
  re-announces the sensor set instead of waiting out the full 60 s.
* CAN: `can_init()` is already retryable (`if (ready) return true`), so the same
  5 s loop drives it. Readiness is read from `can_present()` — the driver's own
  flag — rather than a second copy that could drift.
* Failure logging is throttled: first failure and every twelfth thereafter. An
  absent radio is a wiring state, not an event.
* All transmissions go through one `radio_send()` helper, so "the radio is not up
  yet" cannot be forgotten at a call site. `listen_window()` is likewise gated.

**Recovery from a radio that dies later** is also handled. RF24 with
`FAILURE_HANDLING` (defined unconditionally at `RF24_config.h:23`) raises its
`failureDetected` latch when a transfer never completes — which is distinct from
a frame simply going unacknowledged, since an unacknowledged frame still ends in
`TX_DF` after the retry count expires. That latch is polled each pass and drops
the node back to not-ready, handing the retry loop the re-initialisation the
library's own documentation recommends. Using the library's signal rather than
`write()`'s return value avoids tearing down a perfectly healthy radio every time
no receiver happens to be listening.

Both configurations (`CAN_QUIET_RADIO` 0 and 1) build clean.

### 1.2 CRITICAL — no watchdog

There is no `wdt.h` and no watchdog anywhere in `src/`. The ATmega328P **does**
have one — it is datasheet section 7, "Watchdog Timer" — and it is a one-register
peripheral. Without it:

* Any hang is permanent. (§1.1 was exactly this, and was unrecoverable.)
* Any hang added later — by a library update, a bug, an EMI event — is also
  permanent, with no way to notice.
* The bounded loops in the drivers all depend on `millis()` advancing, and
  `millis()` is Timer0, which the README already flags as sensitive to
  `analogWrite()` on D5/D6. A WDT catches that class of failure too.

#### Verified, not assumed

The register is **`WDTCSR` at I/O `0x60`**, not `WDTCR` — the same
`TIFRx`→`TCIFRx` style rename already handled defensively at `pwm_out.cpp:8`.
`avr-libc` ships `avr/wdt.h` with the full preset range `WDTO_15MS` …
`WDTO_8S`. Compiling against this project's own toolchain emits the real `wdr`
instruction and writes to `0x60`:

```asm
ldi  r24, 0x18 ; WDE|WDCE
in   r0, SREG
cli
wdr
sts  0x0060, r24
out  0x3F, r0
sts  0x0060, r25 ; WDP = 7 = WDTO_2S
wdr
```

Note the `cli` and the SREG save/restore: changing `WDTCSR` requires a timed
`WDCE` handshake, and avr-libc performs it with interrupts disabled. That is
correct, but it means **`wdt_enable()`/`wdt_disable()` must not be called from
inside an ISR** on this part.

#### The gotcha that will bite if this is added casually

On the ATmega328P the watchdog **remains enabled across a reset and comes back at
the fastest prescaler (~15 ms)**, except after a power-on reset. avr-libc's own
header calls this out explicitly:

> *"the watchdog timer remains active even after a system reset (except a
> power-on condition), using the fastest prescaler value (approximately 15 ms). It
> is therefore required to turn off the watchdog early during program startup."*

So the naive `wdt_enable(WDTO_2S)` and nothing else produces a firmware that
**bricks itself on the first reset**: 15 ms is shorter than the ~200 ms that
`sensors.begin()` can take, so the node resets before it ever reaches `loop()`,
forever.

The required shape is:

```cpp
#include <avr/wdt.h>

void setup() {
    wdt_disable();          // MUST be first: still armed at ~15 ms after a WDT reset
    wdt_enable(WDTO_2S);
    ...
}

void loop() {
    ...
    wdt_reset();            // bottom of loop
}
```

Unconditional `wdt_disable()` at every boot is deliberate — it costs a few
cycles and removes the temptation to guard it on a reset-cause flag that can
itself be misread.

#### Cost

`wdt_reset()` compiles to a single `wdr` instruction — **1 cycle**. This loop is
a free-running spin with no `delay()`, so the running total is irrelevant.

#### Two things it is *not* for

* **Not a substitute for the §3.1 dead-man timeout.** The WDT's clock is an
  internal RC oscillator specified at roughly ±10 %, so `WDTO_2S` is really
  anywhere in ~1.8–2.4 s. That is fine for "is the node alive" and useless as a
  command-age measurement. The dead-man timeout belongs in `millis()` time, where
  it can be reported and logged.
* **Not an independent safety net while §3.0 is open.** A WDT reset *is* a reset,
  and a reset lands the output on `PWM_DUTY_PERMILLE` — which §3.0 establishes is
  *heater off*. So adding the watchdog without fixing the boot state converts a
  hang into a repeatable jump to the unsafe state. **Fix §3.0 first, or together.**

Pair it with a boot counter in a retained register (`MCUSR`/`WDRF` in `.noinit`)
reported in the stats frame. That alone converts the invisible brownout-reset
loop from §3.0 into a visible fault.

#### PWM output-path failure: a three-step escalation

`pwmStalled` means the PWM is not working at all — the Timer1 compare vectors
have stopped, so the duty in force is **not being delivered to the load** no
matter what the status frames claim. That is a hardware-path failure and it
escalates rather than taking one action:

| Step | Action | Why |
|---|---|---|
| 1. first occurrence | latch it, log it, **starve the watchdog** and reboot | A reset can genuinely fix this: `pwm_out_begin()` reprograms `TCCR1A`, `ICR1`, `OCR1A`, `OCR1B`, `TCC1B` and `TIMSK1` from scratch, clearing any transient — a lost interrupt, a corrupted timer register, software that clobbered `TIMSK1`. |
| 2. fault present again on the next boot | it is not transient | The latch is in `.noinit`, which survives a watchdog reset (only a power-on reset clears it). A power loss is a fresh start anyway, so EEPROM would spend wear to record a fact nothing acts on. |
| 3. safe mode | `pwm_out_failover_begin()` / `_tick()` — 1 kHz, **200 permille**, timer stopped, **non-blocking** | Unavoidable: if the compare vectors are dead there is no way to make edges with them. The port is driven directly, timed off `micros()` — a polled read of the Timer0 counter, not a compare match, so it does not depend on the thing that failed. |

**200 permille, not 500.** The measured transfer curve puts commanded duty 20 % at
~7 V, comfortably above the ~2.5 V pump cutoff, while 50 % lands near 3.3 V where
the heater may not be driven at all. A failover that does not actually turn the
heater on is worse than useless: it reports FAULT while delivering nothing.

Safe mode is **not a terminal state.** An earlier revision spun in a blocking
loop, which made the mode unreachable — no serial, no CAN, no radio, no way out
but a power cycle. On the wire that is indistinguishable from a crashed node,
which is the opposite of what a diagnostic state is for. The waveform is now
advanced from `loop()` and everything else keeps running: CAN polling, the ADC and
the watchdog all continue.

Two things *are* suppressed, and only because they would wreck the waveform
itself: `listen_window()` (a 250 ms block against a 200 µs half period) and the
verbose stats report (tens of milliseconds of Serial at 115200). A one-line
failover summary replaces the latter.

A bench command (`SYSCMD_REVIVE`, `V` at the receiver) hands D7 back to Timer1.
If the path is genuinely broken the liveness monitor puts it straight back into
failover, so the attempt is bounded and self-limiting rather than a way to escape
a real fault.

The fault is also recorded in the EEPROM ring as `EPL_FLAG_PWM_FAIL` and reported
as `EE_FLAG_PWMFAIL`. `.noinit` is cleared by a power-on reset, so without this a
fault followed by a power blip would leave no trace, could not be reported, and
would reset the escalation history to "first occurrence". A fault record is
**skipped by the restore walk**, because its duty was in force while the output
path was dead and so was never delivered — restoring it would honour a value
known not to have reached the heater.

An earlier revision forced a safe duty on the first stall. That was wrong twice
over: the reboot made it redundant, and forcing a duty calls
`pwm_stop_edges()`, which sets `TIMSK1 = 0`, which makes `pwm_out_active()`
false, which makes the liveness monitor short-circuit on its own guard — so the
fault response disabled the very thing being monitored and no recurrence could
ever be detected.

`PWM_SRC_FAULT` is reported for the whole of safe mode, on both wires, so a
receiver sees the fault rather than inferring it.

### 1.3 FIXED — CAN transmit lockout after three un-ACKed frames

`src/can_sender.cpp:260-276` (`submit`) and `:628-645` in the driver.

`mcp.sendMessage(frame)` scans TXB0/TXB1/TXB2 and uses the first whose TXREQ is
clear; if all three are busy it returns `ERROR_ALLTXBUSY` immediately. The code
turns that into `dropped++` and moves on — which is correct as a *drop*, but it
leaves the underlying state unrepaired.

MCP2515 has auto-retry enabled after `setNormalMode()` (`can_sender.cpp:62`), and
nothing in this firmware ever aborts a pending transmission. So:

1. Any frame that is not ACKed stays with TXREQ set **forever**.
2. Three such frames fill all three TX buffers.
3. Every subsequent `submit()` returns `ERROR_ALLTXBUSY`. This node is mute.
4. It never recovers. `dropped` climbs. `can_recover()` exists
   (`can_sender.cpp:73`) but is never called from anywhere.

The trigger is ordinary: the other node on the bus is rebooting, or the bus is
powered by the same supply as this board, or a transceiver is disabled for a
moment. **On the heating bus this means the node goes permanently silent after
about four seconds and requires a power cycle to come back.**

Note the README documents this as expected behaviour on an *unpopulated* board,
which is fair. The defect is not the observation — it is the absence of a repair.

Remediation, in order of preference:

* Poll TXB0..2 CTRL in `can_pump()` and abort (`TXREQ = 0`) any buffer that has
  been pending longer than a few hundred milliseconds. Abort is the standard
  remedy for an arbitration-lost / no-ACK condition and makes the node
  self-healing without a controller reset.
* Failing that, call `can_recover()` (SPI reset) when `dropped` has grown by N
  within the last M seconds.

**What was implemented.** `tx_abort_stuck()` in `can_sender.cpp`, called from
`can_pump()` before `rx_drain()` so a wedged buffer is returned to the pool before
the pass tries to transmit on top of it:

* Reads `TXB0CTRL` / `TXB1CTRL` / `TXB2CTRL` and tracks how long each has held
  `TXREQ`. Under `TX_STUCK_MS` (250 ms — about two orders of magnitude above any
  legitimate arbitration-plus-ACK time on 125 kbit/s) nothing happens.
* Past the limit, clears `TXREQ` **and** `ABTF`/`MLOA`/`TXERR` in one write.
  Clearing the fault bits too is deliberate: `TXREQ` alone does not reliably clear
  them, and a stale `MLOA` would misdescribe the next stall.
* A `0xFF` readback means no controller on MISO: counts an SPI error and drops the
  pending state rather than trusting a garbage bit.
* Reports the **first** abort only (`txAbortReportLeft`), then stays quiet.
  This is a bus condition, not an event — it must not flood the log.

**A note on the implementation.** The library's `readRegister()`/`modifyRegister()`
and its `TXBnCTRL` enum are all **private** (`mcp2515.h:461`), so the recovery path
uses raw SPI — `spi_read_reg()`/`spi_write_reg()` — matching what
`can_spi_probe()` already did. Two register addresses and four bit defines, rather
than forking or patching the dependency. Worth remembering if the driver is ever
swapped: a replacement may make these public, and then this becomes redundant.

New counters, surfaced in the stats line as `rst=<ok>/<tries> abort=<n>(0x<ctrl>)`:

| Counter | Meaning |
|---|---|
| `can_tx_aborts()` | Buffers aborted. Non-zero ⇒ frames were being discarded because nothing acknowledged them. |
| `can_tx_abort_faults()` | `ABTF`/`MLOA`/`TXERR` of the last one — says *why* it stalled: arbitration lost, message lost, or transmit error. |
| `can_recover_tries()` | Recovery attempts, successful or not. |

### 1.4 FIXED — bus-off was detected but never recovered

**Was:** `CAN_AUTO_RECOVER 0` at `can_sender.cpp:24`. `busOff` was set from EFLG
in `can_stats()` and reported once from `can_pump()`, and the recovery branch was
compiled out. The MCP2515 latches bus-off and the only way out on this board is
the SPI RESET instruction (no `~RESET` pin is broken out), so **once bus-off
occurred, CAN was dead until power was cycled.** Bus-off is reachable in normal
operation — it is what happens when this node transmits into a bus where nobody
ACKs long enough for TEC to reach 256, i.e. the same scenario as §1.3.

The header comment at `can_sender.h:96-98` claimed recovery was automatic; it was
not. Documentation and implementation disagreed, which is itself a finding.

**Now:**

* `CAN_AUTO_RECOVER 1`, with a `RECOVER_BACKOFF_MS` (2 s) between attempts. The
  reset is not free — it drops every queued frame — so an un-backed-off retry loop
  would be an outage of its own making.
* `can_pump()` attempts recovery and reports the attempt with `EFLG`, `TEC`,
  `REC` and a try counter. It returns early while `busOff` is set: nothing else
  can work while the controller is off the bus.
* If `can_recover()` fails it leaves `ready == false`, and the peripheral state
  machine in `main.cpp` falls back to plain `can_init()` retries every 5 s — so a
  dead controller is retried by two independent paths without either looping.
* `configure()` now clears `busOff` and `busOffEflg` on success. This matters: a
  fresh controller has no error counters, so without it the pump would
  immediately re-trigger a recovery for a fault that had already been cleared.

The header comment now matches the code.

Related, still open: EFLG `RXWAR`/`TXWAR`/`RXEP` are printed but never acted on,
so a bus degrading toward bus-off produces no escalating response.

### 1.5 MEDIUM — EFLG is read-to-clear, so bus-off detection is 5 s late

`can_stats()` clears EFLG by reading it. That is intentional and documented, but
it means the only bus-off detector runs on the 5 s `send_can_stats()` cadence.
Combined with §1.4, the worst case is: bus-off occurs at t, is detected at t+5 s,
and is never acted on. Not harmful by itself, but it should be stated explicitly
rather than left implicit.

---

### 1.7 FIXED — `.noinit` trusted across a flash, so a healthy node booted into safe mode

Found on the bench, not in review, and worth recording because the reasoning is
the kind that only fails on the second reflash.

§1.2 puts the escalation latch in `.noinit`, which is deliberate — it must survive
the watchdog reset that step 1 depends on. The assumption behind that is that
`.noinit` is cleared by a power-on reset and nothing else. **That assumption is
wrong in the way that matters:** `avrdude` preserves `.noinit` across a flash
upload. It is not RAM, and the bootloader writes over `flash`/`boot`, not over
`.noinit`.

So the sequence was: node entered failover for any reason, latch set; firmware was
fixed and reflashed; node came up still believing a fault was latched, and went
straight into safe mode on an output path that was perfectly healthy. The symptom
was indistinguishable from a real fault — `src=FAULT`, heater at 200 permille —
and it survived every subsequent reflash, because the act of refitting was the
thing keeping it broken.

`PORF` does not help. On an upload `MCUSR` is not set to `PORF` either, so the
boot-reason test cannot distinguish "freshly flashed" from "watchdog reset", and
a node flashed over USB never reports a power-on reset at all.

Fixed by guarding the latch: `PWM_BOOT_MAGIC` (`0xA70F`) is written alongside the
state on every genuine entry, and the latch is honoured **only** if it matches.
Uninitialised `.noinit` reads as garbage and is ignored; a latch we wrote is
trusted. That is the same argument as the EEPROM format tag in §6b, applied to a
region no `erase` command can reach.

Two consequences worth keeping:

- **`.noinit` is not a safe place to put anything that outlives a flash**, unless
  it carries its own validity marker. It is the right place for a watchdog-reset
  latch and the wrong place for anything else.
- **A flash is not a clean slate.** The EEPROM ring deliberately outlives a flash
  too — that is what makes a fault record survive a power blip — so a node can
  come up reporting `EE_FLAG_PWMFAIL` from a fault that was fixed weeks ago. This
  is correct behaviour being misread as a live fault. `X` at the receiver erases
  the log; the receiver now ages the flag so a historical one can be told from an
  ongoing one.

## 2. Blocking paths that are bounded but damaging

There are **no further infinite loops**. Verified against the actual library
sources in `.pio/libdeps/nano/`:

| Suspected | Verdict | Evidence |
|---|---|---|
| 1-Wire ROM search | Bounded | `OneWire.cpp:465` `while(rom_byte_number < 8)`; `reset()` failure returns early at `:406` |
| `blockTillConversionComplete` | Not reached | `waitForConversion` is set false at `main.cpp:442`, so `requestTemperatures()` returns at `DallasTemperature.cpp:473` |
| MCP2515 `sendMessage` | Bounded | `mcp2515.cpp:636-644` returns `ERROR_ALLTXBUSY`, no wait |
| MCP2515 `setMode` | Bounded | `mcp2515.cpp:191` `while (millis() < endTime)`, 10 ms cap |
| RF24 `write` | Bounded, ~190 ms | see §2.1 |
| RF24 `stopListening` | Bounded | no wait loop in `RF24.cpp:1171-1193` |
| SPI transfers | Bounded | fixed byte count, polled hardware |

### 2.1 MEDIUM — `radio.write()` stalls ~190 ms per frame when the radio is dead

`FAILURE_HANDLING` is defined unconditionally in `RF24_config.h:23`. With it,
`RF24::write()` (`RF24.cpp:1314-1338`) is:

```cpp
bool doRecovery = true;
do { startFastWrite(buf, len, multicast); } while (errHandler(&doRecovery) < 0);
```

`errHandler` (`RF24.cpp:1256-1285`) returns `-1` once on a 95 ms timeout and sets
`*doRecovery = false`, so the second pass returns `0` and the loop exits. **Worst
case 2 × 95 ms ≈ 190 ms per call.** Not a hang, but with a failed radio one cycle
costs roughly:

```
send_temperatures  190 ms
broadcast_pwm      190 ms
send_adc_stats     190 ms
send_can_stats     190 ms   (+ can_spi_probe, §4.5)
send_identities   up to 760 ms  (4 frames)
------------------------------------
                  ≈ 1.5 s of blocked loop
```

Per pass, on top of the 250 ms listen window. The PWM output is ISR-driven so the
duty itself survives, but:

* the ADC peak-to-peak window (§`adc_monitor`) goes blind for ~1.5 s — exactly
  when the radio fault indicates something is wrong with the node;
* `millis()`-based scheduling stretches, and the temperature cadence degrades;
* `errHandler` calls `flush_rx()`, so a PWM command sitting in the RX FIFO from
  the previous listen window is silently discarded.

Worth bounding explicitly if a radio fault is survivable.

### 2.2 MEDIUM — 1-Wire is re-enumerated every cycle, contrary to the code comment

`main.cpp:220-222` claims:

> DallasTemperature caches the bus scan, so getAddress() costs no 1-Wire traffic.

**This is false for DallasTemperature 4.x.** `getAddress(uint8_t*, index)`
(`DallasTemperature.cpp:144-158`) does `_wire->reset_search()` and then re-runs
`_wire->search()` from index 0 up to `index`. `getDeviceCount()` returns the cached
count (`:160`), but `getAddress()` does real bus traffic every time.

`scan_sensors()` calls it once per sensor, so a 4-sensor bus performs
**1 + 2 + 3 + 4 = 10 full ROM searches every 5.8 s**, each of which resets the bus
and walks up to 64 bit slots. At roughly 70 µs per slot that is on the order of
tens of milliseconds of blocking 1-Wire activity per cycle that the design does
not account for.

Two consequences:

* Undocumented, unbudgeted blocking in the temperature path.
* Each bit slot runs inside `cli()`/`interrupts()` in OneWire. Ten searches per
  cycle means on the order of a thousand interrupt-disabled windows every 5.8 s.
  This injects edge jitter into the D7 PWM output that contradicts the README's
  "1 µs duty resolution" claim — ISR entry latency during a 1-Wire slot is exactly
  the delay an edge landing inside that slot suffers.

Re-enumerating is a good *intent* (it is what makes rank meaningful when a sensor
is unplugged), but it should be rate-limited to the identity cadence and the
comment corrected.

---

## 3. No failsafe — the dominant risk for a heating load

This is not a bug in the sense of a typo, but it is the finding that matters most
for a system that has to run unattended for a year.

### 3.0 CRITICAL — Freeze scenario: a power cut in winter leaves the house with no heat, silently

**This is the single worst failure mode of this board, and it is the default
behaviour, not a fault condition.**

Trace it through the code *(line numbers as of the pre-remediation revision — the
radio line no longer hangs, but the rest of the sequence is unchanged and still
live; the restore point that fixes it was item B1.1, now done)*:

```
power cut
  → Nano browns out / resets
  → setup() runs
      main.cpp:460   radio.begin()  ── FIXED: no longer blocks (§1.1)
      main.cpp:489   pwm_out_begin() ── NOW FIRST: D7 driven before anything else
      main.cpp:490   set_pwm_duty(PWM_DUTY_PERMILLE)
                        PWM_DUTY_PERMILLE = 1000   (main.cpp:24)   ← STILL OPEN
                        → pwm_out_set(1000): permille == 1000 endpoint branch
                          → pwm_stop_edges(); PORTD |= PORT_BIT;
                          → D7 driven HIGH, permanently, no PWM, no interrupts
```

So after every reset the output is pinned at the hardcoded constant 1000 permille.
Now what does 1000 permille mean for the load?

From the board's own measured transfer function (README, "Measured output"):

| duty | measured | |
|---|---|---|
| 0 % | 11.57 V | full |
| 100 % | 0.137 V | ~zero |

> *"The response is **inverted** — more commanded duty gives less voltage."*
>
> *"**Pump cutoff.** The pump stops below 2.5 V, which the formula puts at
> **duty ≈ 56 %**. Clamp commanded duty to 55 % or lower; above that the pump is
> off and further increases do nothing."*

100 % duty → 0.137 V → far below the 2.5 V cutoff → **the pump is off and no heat
is being delivered.** The code comment at `main.cpp:24` says exactly this:

```cpp
#define PWM_DUTY_PERMILLE  1000   // GPIO held high → heater off until a CAN command
```

**The boot state of this heating controller is "off".**

There is no EEPROM in the firmware (verified — no `EEPROM` reference anywhere in
`src/`), so this is not "the last PWM is retained". It is a hardcoded constant. And
the second road to the same place: if the *commanding* node dies while this board
keeps running, the board holds whatever duty was last commanded — and §3.1 shows
nothing ages that out. Both roads end at "no heat, indefinitely".

#### Why this is worse than the mirror-image failure

A heater stuck **ON** is loud: the house gets warm, the pump runs, somebody
notices, a breaker may trip. A heater stuck **OFF** is completely silent, and
during a January power cut it produces no observable symptom on this board at all:

* The ADC current coil is measured every second and reported — but as telemetry
  only. Nothing in the firmware compares it to a threshold or acts on it.
* The four DS18B20s are read every 5.8 s and published — but again, telemetry only.
  **There is no thermostat anywhere in this firmware.** No code path compares a
  measured temperature against a setpoint.
* `PWM_FLAG_ACTIVE` is 0 (interrupts off, endpoint branch), which a receiver might
  read as "not modulating" rather than "modulating at 100 %, which means no heat".

So the node reports a perfectly healthy-looking temperature bus at −3 °C while
delivering nothing, and there is no dead-man timeout (§3.1) to age the state out.
It will sit there until a person physically walks into the house.

#### The amplifiers that make it reachable

| Trigger | Mechanism |
|---|---|
| **Plain power cut, long duration** | Coldest conditions are exactly when this happens. Both nodes reset; the controller must re-establish WiFi/HA/schedule before it commands anything. If its own demand logic comes back "no heating required", nothing is ever sent. |
| **Marginal supply / brownout, not a clean outage** | Worse. The Nano can reset *repeatedly*, re-applying the boot duty every time, while the rest of the house has power. With no watchdog (§1.2) and no boot counter, this is entirely invisible — it looks identical to a healthy idle node. |
| **CAN RX starved (§4.3)** | On a busy bus the controller's `0x240` commands are never drained, so the board never leaves its boot duty even though the controller is healthy and commanding. Silent. |
| **`radio.begin()` fails at boot (§1.1)** | `while(1){}` fires *before* `pwm_out_begin()`. D7 stays a high-impedance input for the whole outage — the driver input floats and the load state is **undefined**, not merely off. |
| **Radio write stalls (§2.1)** | Not a freeze by itself, but it delays the temperature cadence during which nothing is acting on temperature anyway. |

#### The policy was also resting on an unverified assumption — since resolved

`main.cpp:24` encodes the entire failsafe policy of a mains-adjacent heating
controller into one character: `1000`. The reasoning — "GPIO high = heater off" —
inverts through a PC817 optocoupler into a TL431LP shunt sink. and the README was explicit that the polarity had not been established:

> *"**These are not D7 measurements.** 11.57 V exceeds the ATmega328p supply, so
> the probe is on a downstream node, not the MCU pin. The probe point and the
> driver stage are not documented yet."*

The safety-critical assumption was therefore: a **measured, downstream,
undocumented** transfer function, on an **inverting** stage whose direction was
observed rather than designed, with a **PC817 CTR that is a 50–600 % distribution
varying with temperature and age**. If any part of that chain were wrong, the boot
state would not be "off" — it would be full power, silently, from a cold start.

**This is now closed (B1.2).** The external circuit was traced and the polarity
verified: the stage is a PC817 optocoupler into a TL431LP shunt sink, the response
is **inverting** as observed, and the two endpoints — 11.57 V at 0 % and 0.137 V
at 100 % — sit close enough to the rails to show D7 is swinging fully and that
duty is reaching the load rather than being dropped in the wiring. See the
"Measured output" and circuit-analysis sections of the README.

Two things survive the verification, and are worth stating plainly rather than
letting "verified" imply more than it does:

- **Verified inverting at the probe, not characterised over time.** The PC817 CTR
  is a distribution that shifts with temperature and age. The *direction* is
  established, so the boot state is no longer resting on a guess; the *margin*
  between commanded duty and delivered power still has ageing drift in it.
- **The mid-range is markedly non-linear** — 20 % duty reads 6.99 V where a linear
  inverting stage would give 9.6 V. That is why safe mode uses 200 permille as a
  *measured* point rather than treating it as 20 % of anything.

#### What the boot state should actually be

**This section was wrong in the first revision of this document.** The
recommendation there was a local frost-protection thermostat, on the reasoning
that the board already has four DS18B20s and `set_pwm_duty()` sitting idle.
That overlooks what the firmware knows about *which* reading matters:

- Sensors are ranked by **ROM address** (`sort_by_rom`, `main.cpp`), so rank is
  arbitrary and shifts whenever a sensor is unplugged. There is no mapping from
  rank to a room.
- No setpoint exists anywhere in the firmware.
- `PWM_DUTY_PERMILLE` is a fixed constant, not derived from any measurement.

The node has no idea which reading represents the space that would freeze, and
no value to compare it against. **Frost protection is not implementable here** —
that is missing information, not missing effort, and the original recommendation
should not have been written as though it were a few lines of work.

What *is* implementable is state transfer: the upstream controller does know the
setpoint and the location semantics, so its decision has to survive a restart of
this board. That means **persisting the commanded duty and restoring it on
boot** — which inverts this document's earlier position that the stored duty
must never be written back.

| Policy | Boot behaviour | Verdict |
|---|---|---|
| A. Boot OFF *(current)* | `1000` permille until the first `0x240` | Protects the load. **Unacceptable for a winter-critical system.** |
| B. Boot to a fixed moderate duty | e.g. 350 permille | Bounded but crude: ignores what the controller actually asked for. |
| C. Local frost thermostat | — | **Not implementable.** Missing the location semantics and any setpoint. |
| **D. Restore the persisted duty** *(adopted)* | Whatever the controller last commanded, held across a reset | Continuity across short outages, and the boot state reflects a real decision rather than a constant. |

#### Why D mitigates the freeze rather than merely relocating it

The objection to D is that persisting the duty makes a bad value *sticky*. That
is real, but it is bounded by the fact that **in winter the persisted duty is a
heating duty**. The duty is not a guess about the season — it is whatever the
controller, which does know the setpoint and the location semantics, last
decided. Recent heating usage *is* the evidence that the heating period has
started, so there is nothing for a frost thermostat to add that the duty does not
already carry.

The exposure is therefore only the window where both are true at once: the duty
is low **and** the house has cooled enough to matter. During a heating period
that combination does not arise, because the controller would not have stopped
commanding heat. The realistic edge cases are a shoulder-season spell — where
restoring "off" is the correct answer anyway — and a controller that died *and*
had stopped demanding heat, which is not distinguishable from a deliberate off.

#### No dead-man timeout — a deliberate rejection

This document originally recommended a command timeout: expire the duty if no
`0x240` arrives within N seconds, so a dead controller could not hold it
indefinitely. **That recommendation was wrong, and has been withdrawn.**

The reasoning error was treating the duty as a *lease*. A lease has to expire,
because it was granted on information that may since have changed. But "hold the
house at 55 %" is not a lease — it remains true for as long as nobody says
otherwise. Expiring it would:

* drop the heat every time the controller **merely rebooted**, which is a far
  more common event than a controller dying while still demanding heat, and
  a reboot is precisely when the duty is most needed;
* convert a harmless controller restart into a frozen house — reintroducing the
  very failure the persistence exists to prevent;
* and it could not distinguish "controller died while demanding heat" from
  "controller died having stopped", so it would discard the *correct* value as
  often as the wrong one.

The cost accepted instead: **a duty can outlive its author.** That is correct for
a setpoint and wrong only if the controller was acting on information that has
since changed — which is the controller's problem to fix when it returns, not
this node's to guess at by inventing a safe value.

What replaces the timeout is **making the source visible** rather than bounding
the value. `PWM_SRC_*` travels in bits 1..2 of the PWM frame's flags byte, on
both wires at once:

| Source | Meaning |
|---|---|
| `EEPROM` | restored from the log; **nothing is commanding this node** |
| `CAN` | last valid command was CAN `0x240` |
| `NRF` | last valid command was radio `0xABD0` |
| `FAULT` | local safety action, i.e. the PWM ISR stall fallback |

A node sitting on `EEPROM` forever is then *legible* as such — which is the
diagnostic a timeout was being asked to provide, without the behaviour change
that made it unacceptable.

What the design still owes:

1. **The 8 h refresh**, so a restored duty is never more than a few hours stale.
   24 h was rejected precisely because it leaves a full night between writes.
2. **The polarity verified at the load** (Batch 1, item B1.2). No longer a boot-constant
   question: the whole log is expressed relative to `PWM_DUTY_PERMILLE`, so the
   measurement changes that constant and nothing else.

So D is chosen and the freeze risk is judged **mitigated, not eliminated** —
with (1) and (2) as the conditions under which that judgement holds.

`main.cpp:125-132`:

```cpp
// Last writer wins: no ramping, no minimum-on/off pulse, no other validation
// than the permille clamp inside pwm_out_set().
static void set_pwm_duty(uint16_t permille)
```

What is missing:

| Missing control | Consequence |
|---|---|
| ~~Command timeout / dead-man switch~~ | **Rejected, deliberately.** The duty is state, not a lease — see "No dead-man timeout" in §3.0. Replaced by `PWM_SRC_*` reporting, which makes the same fact visible without changing behaviour. |
| Sequence number or heartbeat on PWM commands | A stale frame sitting in an RX buffer, or replayed by a buggy peer, is applied as if fresh. |
| Ramp / slew limit | A single frame can step the duty 0 → 100 % instantly. |
| Upper duty clamp | The README's own measurement puts the pump cutoff at ~55 % duty. Nothing enforces it; a command of 1000 permille is accepted verbatim. |
| Minimum on/off time | Chatter at any frequency is passed straight to the load. |
| Any use of the current measurement | `send_adc_stats()` publishes the coil current purely as telemetry. A genuine over-current condition is measured, reported, and never acted on. |
| **Any thermostat at all** | The four DS18B20s are read every 5.8 s and published. No code path compares any of them against a setpoint. The board measures temperature and does nothing with it — see §3.0. |
| Boot-state validation | Boot duty is `PWM_DUTY_PERMILLE 1000` (`main.cpp:24`). That is "safe" only under the inverting driver the README documents. The polarity assumption is external, undocumented in the code, and unenforced. |
| Brownout / BOD | Default fuse settings. A sagging supply resets the node silently to the boot duty. |
| Fault latch | Nothing latches a fault into EEPROM or into a CAN frame. |

**The highest-value change on this list is the EEPROM restore point** (§6b), and
it is now implemented: the duty survives a restart, so the freeze scenario is
mitigated rather than merely analysed. What replaced the dead-man timeout is
source reporting — `PWM_SRC_EEPROM` in the status frames is this node saying
"I am holding a restored duty and nothing is commanding me", which is the
honest form of the alarm the timeout was trying to raise.

Note the asymmetry that makes this worse: the bus is designed so that everything
still works when the radio is dead (§1.1 fail-soft, CAN fail-soft). That is good
engineering for availability, but it means a silent total failure is exactly what
the rest of the system is designed not to notice.

---

## 4. Data-integrity and protocol risks

### 4.1 HIGH — three sensor fault codes escape as plausible temperatures

`main.cpp:65-87` (`raw_to_cdeg`) maps exactly three of the DallasTemperature
failure sentinels to `ERR_FLAG`:

```cpp
if (raw == DEVICE_DISCONNECTED_RAW ||
    raw == DEVICE_POWER_ON_RESET_RAW ||
    raw == DEVICE_INSUFFICIENT_POWER_RAW) return ERR_FLAG;
```

DallasTemperature 4.0.6 defines **five** (`DallasTemperature.h:35-56`), and all
three of the unmapped ones are reachable — `calculateTemperature()` at
`DallasTemperature.cpp:539-550` returns `DEVICE_FAULT_OPEN_RAW` (-32512),
`DEVICE_FAULT_SHORTGND_RAW` (-32384) and `DEVICE_FAULT_SHORTVDD_RAW` (-32256) for
any sensor on the `DS1825MODEL` (0x3B) family with a fault bit set.

Unmapped, they pass through the arithmetic as valid readings:

| Sentinel | Raw | Reported as |
|---|---|---|
| `DEVICE_FAULT_OPEN_RAW` | −32512 | **−253.98 °C** |
| `DEVICE_FAULT_SHORTGND_RAW` | −32384 | **−252.98 °C** |
| `DEVICE_FAULT_SHORTVDD_RAW` | −32256 | **−251.98 °C** |

None of these is `ERR_FLAG`, so `present` still has the bit set and the frame
looks like a healthy sensor reporting an extreme temperature. A shorted sensor is
indistinguishable from a very cold room — and −252 °C is a value that would drive
a heater to maximum demand.

Fix: map all five, and preferably invert the logic — treat *any* raw value at or
below `DEVICE_FAULT_OPEN_RAW` as an error, so a future library adding a sixth
sentinel cannot leak through.

### 4.2 HIGH — a DS18B20 CRC error is reported as 0.00 °C

`DallasTemperature::getTemp()` (`:283-294`) returns `DEVICE_DISCONNECTED_RAW` when
`isConnected()` fails, which includes the scratchpad CRC check at `:175`. That
*is* mapped correctly — so this specific path is fine.

The residual risk is that a genuine reading of exactly `DEVICE_DISCONNECTED_RAW`
(−7040, i.e. −55.00 °C) is indistinguishable from the error. This is inherent to
the library's sentinel scheme and cannot be fixed here, but it should be recorded
because −55 °C is a plausible value for an unheated sensor in winter.

### 4.3 HIGH — `readIntfStable` can livelock and starve the CAN receiver

`can_sender.cpp:173-227`

```cpp
static bool readIntfStable(uint8_t* out) {
    uint8_t a = mcp.getInterrupts();
    uint8_t b = mcp.getInterrupts();
    uint8_t c = mcp.getInterrupts();
    ...
    if (a != b || b != c) return false;
    ...
}

static void rx_drain() {
    for (uint8_t n = 0; n < 2; n++) {
        if (!readIntfStable(&intf)) return;   // <-- bails out entirely
        ...
```

Requiring three identical reads is a good defence against the single-stray-`0x00`
case described in the comment. The failure mode it introduces is that on a busy
bus — where INTF genuinely changes between the three reads, i.e. frames arriving
faster than ~30 SPI round-trips — `readIntfStable` returns false *every* time and
`rx_drain()` returns without ever draining a buffer.

When that happens the MCP2515's two RX buffers fill, RX0OVR/RX1OVR latch, and
**PWM commands on `0x240` are never received at all** — while `received` still
climbs, so the counters look healthy. The safety-critical input path starves
quietly precisely when the bus is busiest.

Note also that `listen_window()` calls `can_pump()` in a tight spin loop
(`main.cpp:153`), so `rx_drain` is being invoked at several kHz there — three SPI
round-trips per iteration is significant avoidable bus traffic, and it makes the
three-reads-agree requirement harder to satisfy, not easier.

Remediation: accept two agreeing reads and treat the third as advisory, or clear
the RX flag and read the buffer anyway when the flag is set, rather than requiring
the flag to be stable *before* acting on it.

### 4.4 MEDIUM — PWM commands can be injected through the broadcast pipe

`RF24::stopListening()` (`RF24.cpp:1192`) ends with:

```cpp
write_register(nRF24L01::EN_RXADDR, read_register(nRF24L01::EN_RXADDR) | _BV(ERX_P0));
```

ERX_P0 is therefore always enabled on the L01+, in transmit mode as well. When
`listen_window()` calls `startListening()`, **pipe 0 — the broadcast address
`NRF_BC_ADDR` — is live for 250 ms every cycle.** Any frame broadcast to that
address by any node is delivered.

`listen_window()` does check `magic != NRF_MAGIC_PWM_CMD` and rejects
(`main.cpp:176`), so only a frame explicitly carrying `0xABD0` broadcast to
`NRF_BC_ADDR` would be accepted — and that frame would be accepted by *every* node
simultaneously. The protocol intends commands to go to `NRF_CMD_ADDR` on pipe 1.

Low probability, but it means the duty output is commandable by anyone who knows
the broadcast address, with no addressing check. Worth a filter on the pipe number
(`radio.available(&pipe)`) before acting on a command.

### 4.5 MEDIUM — `can_spi_probe()` corrupts an in-flight CAN frame (partially fixed)

**Was:** `can_spi_probe()` wrote a 13-byte garbage pattern directly into
**TXB0SIDH..TXB0D7** with no regard for whether TXREQ was set on TXB0, and
`send_can_stats()` called it unconditionally every 5 s.

Per the MCP2515 datasheet, TXBnSIDH/EID8/D0–D7 must not be written while
TXBnCTRL.TXREQ = 1. Given §1.3, TXB0 is *frequently* stuck with TXREQ set — so the
probe was routinely overwriting a frame still being retried. It also injected a
bogus frame onto the bus: the pattern's first byte is 0xA5 with EXIDE set, so it
goes out as an **extended-ID** frame with a garbage ID. Standard-ID-only receivers
discard it, so the practical risk was low — but the probe was destroying real
traffic every five seconds to diagnose a link that is normally fine.

**Now fixed:** the probe reads `TXB0CTRL` first and returns `0xFF` (reported as
"no answer") if TXREQ is set, so it can never clobber a frame that
`tx_abort_stuck()` has not already cleared.

**Still open:** the probe runs unconditionally every 5 s even on a perfectly
healthy bus. It has no diagnostic value there and costs two SPI round-trips plus
a register write. Gate it on an actual fault — `can_spi_errors() != 0` or a
previously-observed probe failure — and run it rarely rather than constantly.

---

## 5. Observability defects

These do not cause faults, but on a 24/7 node they are the difference between a
detected and an undetected failure.

### 5.1 MEDIUM — every health counter is 16-bit

`sent`, `received`, `dropped`, `rxMalformed`, `rxOverflows`, `spiErrors`,
`recoveries` (`can_sender.cpp:34-39`) and `win_frames`/`win_cmds`/`win_bad`
(`main.cpp:103-105`) are all `uint16_t`.

At the observed cadence the node emits roughly one CAN frame per 700 ms including
identity frames, so **`sent` wraps roughly every 12 hours** and the identity-heavy
counters sooner. A counter that has wrapped looks identical to a node that has
stopped transmitting. `okCount`/`failCount` in `main.cpp:521` are already
`uint32_t`, so the pattern exists in the codebase — extend it.

### 5.2 LOW — ISR edge counters are read non-atomically

`pwm_out.cpp:14-15` declares `rising`/`falling` as `volatile uint32_t` and returns
them straight from main-loop context (`:114-115`). A 32-bit read on an 8-bit AVR
is four non-atomic instructions; a Timer1 compare ISR firing in the middle yields
a torn value. Diagnostics only, but it will occasionally produce an edge count
that never could have occurred, which is exactly the sort of thing that costs an
afternoon.

### 5.3 LOW — `win_bad` is polluted by other nodes

Because pipe 0 is live during the listen window (§4.4), every broadcast from every
other transmitter on the network increments `win_frames` and then `win_bad`. The
counter documented as "foreign magic or bad CRC" cannot distinguish a corrupted
frame from normal traffic, which defeats its stated purpose.

### 5.4 LOW — heap allocation in the temperature path

`cdeg_to_str()` and `rom_to_str()` (`nrf_payload.h:198, 271`) return `String`, so
`main.cpp:452` and `:584` perform heap allocation inside the cycle loop. At ~4
short-lived allocations per 5.8 s this is unlikely to fragment with 962 B of
static headroom, and Arduino's free-list handles same-size churn well — so this is
low risk. It is still avoidable: both are trivially replaced by a
`char[N]`-emitting helper, which would also remove the last heap use from the
firmware entirely.

---

## 6. Timing and rollover review

### 6.1 Correct

The unsigned-difference idiom is used consistently and correctly:

* `listen_window()` — `while ((long)(until - millis()) > 0)` (`main.cpp:152`)
* temperature cadence — `now - lastCycleMs >= TEMP_PERIOD_MS` (`main.cpp:554`)
* conversion gate — `now - convStartMs >= CONVERSION_MS` (`main.cpp:561`)
* ADC window — `(unsigned long)(millis() - windowStartMs) >= ADC_WINDOW_MS`
  (`adc_monitor.cpp:106`)
* CAN stats — `millis() - lastStatsMs >= STATS_PERIOD_MS` (`main.cpp:534`)

All survive the 49.7-day `millis()` wrap correctly.

### 6.2 One non-idiomatic compare

`main.cpp:616`

```cpp
if (now >= IDENTITY_BOOT_MS) idPeriodMs = IDENTITY_PERIOD_MS;
```

This is a raw comparison, not a difference, so it is not wrap-safe. It happens to
be harmless: by the time `millis()` wraps, `idPeriodMs` has long since been
promoted to 60 s and is never reset. Still, it reads as a latent bug and should be
written as a difference for consistency.

### 6.3 Cycle budget

Normal cycle: 5000 ms wait + 800 ms conversion + ~10–45 ms of 1-Wire enumeration
(§2.2) + 250 ms listen + ~40 ms of serial. Roughly 6.1 s, comfortably inside the
5 s cadence with no pile-up. With a dead radio it stretches to ~7.6 s (§2.1). No
overflow, no starvation — but the 1-Wire cost is not in anyone's budget because
the comment says it is free.

### 6.4 Division by zero — none

Checked explicitly, since it was asked about. Every division in the firmware has a
non-zero divisor:

* `abs_raw / 128`, `abs_raw % 128` — literal
* `(frac_128 * 32) / 25` — literal (`nrf_payload.h:83`)
* `permille * PWM_TICKS / 1000UL` — literal (`pwm_out.cpp:99`)
* `s.sum / (s.readings ? s.readings : 1)` — explicitly guarded (`main.cpp:321`)
* `tmp /= 10` — literal (`nrf_payload.h:210`)

No variable ever reaches a division as a divisor. **No division-by-zero is
reachable.**

---

## 6b. EEPROM restore-point ring — design and verification

Implemented in `src/eeprom_log.{h,cpp}` (+ the 20-line AVR binding in
`eeprom_avr.cpp`), with a host test harness in `test/test_eeprom_log/`.

### Why it exists

§3.0 established that frost protection is not implementable on this node, so the
duty has to survive a restart instead. That makes the stored value a
**state-transfer mechanism**, not telemetry — which in turn means it has to be
right about power loss, because a power cut is the exact event it exists for.

### Format

```
record, 4 bytes, 256 slots across the full 1 KB:
  [0..1] word = duty(10 bits) | flags << 10          little-endian
  [2]    reserved, always 0
  [3]    chk — rot-folded checksum, written LAST
```

4 bytes rather than 3: it lines up with the ATmega328p's 4-byte EEPROM page, so
each record is exactly one page write. A 3-byte record straddles a page half the
time. Bits 11–15 are spare for future flags.

`chk` is rot-folded, not a plain XOR, because XOR is blind to a byte swap —
swapping `word_lo` and `word_hi` leaves a pure XOR unchanged and turns duty 800
into 8192 undetected. Two extra rotations close it.

### No counter cell, and why

A 2-byte monotonic counter would make "where is the newest record" pure
arithmetic — genuinely better in every respect except one: it takes **every**
write. That was rejected on concentration, not on date. The harness measures it
(§ "wear" below) at **129× the worst ring cell** after ten years. Both designs
survive ten years on the datasheet figure; only one of them has a cell carrying
the entire design's endurance, and it is the cell whose failure would take down
the chain.

### The invariant that replaces it

`EPL_VALID_MAX` (240) keeps the valid region strictly shorter than the ring, so
**the empty region is always exactly one contiguous arc** and the valid region is
its complement:

```
empty arc = [e_start .. e_start + L - 1]
oldest    = (e_start + L) % SLOTS
newest    = (e_start - 1 + SLOTS) % SLOTS
```

`e_start` is the empty slot whose predecessor is **valid** — the arc's start,
not merely the first empty slot seen. That distinction is not academic; see the
bugs below.

This is not about wear. It is about the scan being unambiguous: were the ring
ever 100 % full there would be no empty arc and the newest would be
unidentifiable. The append path enforces the cap *before* writing, so the
ambiguous state is unreachable even transiently.

### Two things that must not be conflated

- **Erased vs corrupt.** A record failing its checksum is *not* treated as
  empty. Merging the two would split the empty arc in two, which is the one
  property the scan depends on. A torn write therefore leaves a hole, and the
  scan reports it rather than silently mis-locating.
- **`0xFF` vs `0x00`.** AVR EEPROM erase sets all bits to **1**. There is no
  special "erased" sentinel in the code: a virgin slot simply fails the checksum
  like anything else.

### Test harness — `test/test_eeprom_log/`

The failure this must survive is power loss part-way through a record write, at
every byte offset, thousands of times. That is not reproducible on the bench, and
simavr cannot inject a supply collapse mid-EEPROM-cycle — it builds a whole
firmware image and traces it. So the algorithm is pure C with an injected EEPROM
interface, compiled unchanged for the host against a mock.

```sh
make -C test/test_eeprom_log test        # 517 684 checks
make -C test/test_eeprom_log sanitize    # same, under ASan + UBSan
make -C test/test_eeprom_log compiler    # show which gcc
```

The mock models both things the bench cannot: **power cuts** (a byte budget that
refuses writes past a limit) and **per-cell wear** (counted only when a byte
actually changes, matching `EEPROM.update()` semantics).

Fixed-width types throughout, with `-Wconversion -Werror`. `int` is 16-bit on
the ATmega and 32-bit on the host, so a stray one would make the harness disagree
with the firmware for reasons unrelated to the algorithm.

### Bugs the harness found

Five real defects, all of which would have produced a wrong restore from a power
cut or a blank device:

| Bug | Symptom | Cause |
|---|---|---|
| Scan ignored the gap | A wrapped region `[17..255]+[0]` reported as **un-wrapped** | Compared first-vs-last valid slot. `first=0`, `last=255`, so `first <= last` — the gap in the middle is invisible to an endpoint comparison. |
| Circular predecessor missed | An empty arc at index 0 never found | Scan seeded `prev_valid = 0`, so an arc `[0..15]` beside a valid slot 255 was not recognised as an arc. |
| Valid count undercounted | Healthy rings flagged anomalous | Pass 1 broke at `e_start` before counting the rest, failing the partition check. |
| First append skipped slot 0 | Ring started at slot 1 | `slot = (newest + 1) % SLOTS` is 1 when `newest` is 0 on a virgin ring. |

| Restore ignored the newest record | A newest of "heating" followed by an "off" record returned *off* | The walk started at `i=1` and never tested slot 0 of the window. Right answer, wrong reason — and it discarded the newest's own duty from the minimum. |

Found later, by asking what a **blank** EEPROM does rather than only a virgin
one: `epl_restore()` did not honour the scan's `anomaly` flag, so on an
unlocatable ring it would read the slot the scan had fallen back to and return a
random permille. The property is now asserted directly — *restore must never
return a duty that was not deliberately written* — across virgin, all-zero,
random and partial EEPROMs.

### Measured wear

Ten years of 8 h heartbeats, 10 950 records:

```
total = 86 640 byte writes
worst cell = 85 writes      (across all 1024 cells)
cells used = 1024 / 1024
skew = 1011 / 1000          (1000 = perfectly even)
```

Against the datasheet's 100 000 cycles per cell that is **~1176 years**, and
every cell is used — no idle majority absorbing a hot minority.

### Cadence

| | |
|---|---|
| Boot | one record, written after the scan (`epl_write_boot_marker()`) |
| Steady state | heartbeat every 60 s, **only if the duty has moved since the last record** |
| Idle cost | zero — a node nobody commands never writes |
| Worst-case cost | 1440 writes/day ÷ 240 slots ≈ 6 per cell per day, ≈ 21 900 per cell over 10 y |
| Restore window | last 10 records ≈ 10 min |
| Ring history | 256 slots ≈ 2.8 days |

**This replaced a design that did not work, found on the bench.** The nominal
cadence was "8 h uptime, or a duty change with ≥1 h since the last write". The 1 h
was introduced in `1d1fd57` as *"the backup for the 8 h heartbeat"* — and the 8 h
heartbeat was never implemented, so that one number became the only
change-triggered write path in the firmware. Because `millis()` restarts on boot,
its window opened after *every* boot, so a node power-cycled more often than
hourly — which is exactly what a marginal supply does — would never persist a
commanded duty at all.

Bench confirmation: duty commanded to 500 shortly after boot, then power-cycled.
The node came back on the previous value, with `rec` unchanged. Freeze protection
that does not survive a power cut on the schedule this node actually sees.

Two things about the replacement are load-bearing:

- **The heartbeat only fires when the duty has moved.** `epl_persisted_duty`
  tracks the last duty written by something the restore walk *honours*, and a
  fault record does not count towards it — the walk skips those, so the duty
  behind one is not what a restore would find, and letting it count would mask a
  real duty change.
- **The cadence is a floor, not a ceiling.** The previous design wrote on every
  duty change subject to an hour's gate, so a controller re-sending the same value
  every second still spent a cell once an hour. Nothing here can do that.

The residual window is one heartbeat, not zero: a duty change followed by power
loss within 60 s restores the previous value. Sixty seconds is the right trade for
a heating load — long enough that it cannot be worn out, short enough that a
freeze does not begin at it.

Note the boot marker cannot cover this, and is not trying to. It records
`epl_restored`, which is assigned once at `epl_restore_boot()` and never updated,
so it re-stamps *what was restored*, not *what was in force when power was lost*.
At boot the command has not happened yet.

---

## 7. Mitigation action plan

Ordered by what actually changes the failure behaviour of the system. Items are
grouped into four batches; the batch boundaries are where it makes sense to stop
and test on the bench.

### Batch 1 — safety policy (do these before the board goes back into service)

Row IDs are `B<n>.<m>` — *batch* number, then item within that batch. They are not
section numbers: the watchdog is §1.2 and it has been done since `8a46ecb`.

Nothing below is optional. The board currently has a boot state that means "no
heat" and a control path with no timeout, and it is being asked to run unattended
through winter.

| # | Action | Risk addressed | Effort |
|---|--------|----------------|--------|
| B1.1 | ~~**Wire up the EEPROM restore point.**~~ — **DONE.** Scanned on boot, restored through `pwm_out_begin()`, boot marker appended, write triggers in `set_pwm_duty()`. Verified on the bench: a node powered up with nothing commanding it came back as `duty=1000 src=EEPROM(restored)`. | §3.0 | — |
| B1.2 | ~~**Verify the drive polarity at the load**~~ — **DONE.** The external stage was traced (PC817 → TL431LP shunt sink) and the polarity confirmed **inverting**, with both endpoints near the rails, so duty demonstrably reaches the load. This also settles the safe-mode duty: 200 permille is the measured 6.99 V row, not a nominal fraction. | §3.0 | — |
| B1.3 | ~~**Add a dead-man timeout**~~ — **REJECTED.** The duty is durable state, not a lease; expiring it would drop the heat on every controller reboot. See "No dead-man timeout" in §3.0. | §3.1 | — |
| B1.4 | **Clamp commanded duty** to the range the hardware supports (~550 permille, per the README's own 56 % pump-cutoff measurement) and **ramp** rather than step. | §3.2 | S |
| B1.5 | **Set the HEAT_ON predicate** — the single named constant the ring is built around. B1.2 has now established the direction (inverting), which is what this needed; the remaining question is the usable duty range, which is B1.4. | §6b | S |

**Frost protection is *not* on this list, deliberately.** It is not implementable
on this node — no location semantics for the sensors, no setpoint. See the
correction in §3.0.

### Batch 2 — hang recovery (do before unattended operation)

| # | Action | Risk addressed | Effort |
|---|--------|----------------|--------|
| B2.1 | ~~**Enable the watchdog**~~ — **DONE.** `wdt_disable()` first in `setup()`, `wdt_enable(WDTO_2S)`, `wdt_reset()` at the bottom of `loop()` and inside `listen_window()`. Plus a separate PWM-ISR liveness monitor that forces a safe duty rather than resetting. | §1.2 | — |
| B2.2 | **Boot counter** — **PARTLY DONE.** `.noinit` + `PORF` is in place and reported on the boot line. But it does **not** survive power loss: on `PORF` the value is discarded, so a power cut still reports a clean `boot=1`. The log in §6b carries the reboot flag across power cuts, which is what actually closes this. Unrelated but found while fixing 1.7: the *same* `.noinit` region also holds the PWM fault latch, so it needed the same treatment — see 1.7. | §3.0, §1.2 | S |
| B2.3 | **Map all five DallasTemperature fault sentinels** in `raw_to_cdeg()`, and invert the test to `raw <= DEVICE_FAULT_OPEN_RAW` so a future library release cannot leak a new one through. | §4.1 | S |

2.1 and 2.3 are both small and independent of 2.2.

### Batch 3 — protocol and CAN correctness

| # | Action | Risk addressed | Effort |
|---|--------|----------------|--------|
| B3.1 | **Relax `readIntfStable`** so a busy bus cannot starve the RX path — two agreeing reads with the third advisory, or clear-and-read when the flag is set rather than requiring it to be stable first. The PWM command path is the safety-critical input and it currently goes quiet exactly when the bus is busiest. | §4.3 | M |
| B3.2 | **Check the RX pipe number** in `listen_window()` before accepting a command, so a `0xABD0` broadcast to `NRF_BC_ADDR` cannot reach every node at once. | §4.4 | S |
| B3.3 | **Gate `can_spi_probe()`** on an actual SPI fault instead of running it every 5 s on a healthy bus. | §4.5 | S |
| B3.4 | **Rate-limit `scan_sensors()`** to the identity cadence, and fix the incorrect comment at `main.cpp:220-222` — `getAddress()` re-runs a ROM search every call in DallasTemperature 4.x; it is *not* cached. This is also what removes the ~1000 interrupt-disabled windows per cycle that jitter the PWM edges. | §2.2 | S |

### Batch 4 — observability

None of these prevent a fault. All of them make one visible, which on a node that
runs for a year unattended is worth almost as much.

| # | Action | Risk addressed | Effort |
|---|--------|----------------|--------|
| B4.1 | **Widen the health counters to `uint32_t`** — `sent`, `received`, `dropped`, `rxMalformed`, `rxOverflows`, `spiErrors`, `recoveries`, and `win_frames`/`win_cmds`/`win_bad`. `sent` wraps in ≈12 h today, and a wrapped counter is indistinguishable from a silent node. `okCount`/`failCount` already use `uint32_t`, so the pattern exists. | §5.1 | S |
| B4.2 | **Copy the ISR edge counters under `noInterrupts()`** in `pwm_out_rising_edges()`/`pwm_out_falling_edges()`. A 32-bit read is four non-atomic instructions on AVR and occasionally yields an edge count that never could have happened. | §5.2 | S |
| B4.3 | **Write the identity cadence compare as a difference** — `now - IDENTITY_BOOT_MS >= 0` — for consistency with the rest of the file. Currently harmless; latent. | §6.2 | S |
| B4.4 | **Replace the `String` returns** in `cdeg_to_str()`/`rom_to_str()` with fixed-buffer emitters, removing the last heap use from the firmware. | §5.4 | S |

### Considered and deliberately deferred

| Item | Why not now |
|------|--------------|
| **A clean-shutdown flag in EEPROM** | Proposed in an earlier draft and **rejected**. This node is never shut down by intent — there is no exit path at all — so every boot follows an unclean event and the flag would be clear every time, carrying zero information. |
| Local frost protection | Not implementable here: sensors are ranked by ROM address with no location semantics, and there is no setpoint anywhere in the firmware. See the correction in §3.0. |
| Act on the ADC current measurement as an over-current cut-out | Genuinely valuable — the coil is on A0 and it is the only real closed-loop protection available. But it needs a calibrated threshold, and Batch 1 (B1.2) says the calibration story is not yet sound. Revisit after Batch 1. |
| Escalating response to EFLG `RXWAR`/`TXWAR`/`RXEP` | The frames are now actionable via `can_recoveries()`/`can_recover_tries()`. A warning-band policy is only worth having once there is something on the bus worth protecting. |
| Removing the 190 ms `radio.write()` stall (§2.1) | Bounded, and the recover-on-`failureDetected` path already restores service after a radio fault. Only worth attacking if the ADC sampling gap is measured to matter. |
| Cleaning up the duplicated `can_spi_probe()` byte loops | Cosmetic. The helpers now exist and could be reused, but touching working SPI code for tidiness is a poor trade. |

### Verification before leaving the bench

Each of these is a fault that used to be permanent. Confirm each actually
recovers, rather than trusting the code review.

| Test | Expected result |
|------|-----------------|
| Disconnect the nRF24, power-cycle | Node boots, temperatures still flow over CAN, log says `nRF24 not responding (attempt 1)`; radio comes up when reconnected |
| Pull the CAN bus so nothing ACKs | `abort=` climbs, then **transmission resumes** — this is the §1.3 lockout regression test |
| Force bus-off (e.g. hold the bus low) | `[CAN] bus-off … recovered`, `rst=1/1`, and CAN keeps working afterwards |
| Unplug the crystal / hold RESET | WDT fires, node restarts, boot counter increments and is reported |
| Remove a sensor mid-run | Rank shifts, identity frames follow; an unplugged sensor reports `ERR_FLAG`, never a plausible temperature |
| Power-cycle mid-winter with no CAN controller | The restored duty comes back (not `1000`), and the boot marker is appended. This is the regression test for §3.0 |
| Power-cycle with a virgin or scribbled EEPROM | Restores OFF, never a random permille — see test A2/A3 in the harness |
| Let it run a week | No counter has wrapped; `sent` is monotonically increasing |
| `make -C test/test_eeprom_log test` | 517 684 checks, 0 failures — and it must stay that way after any change to `eeprom_log.cpp` |
| `make -C test/test_pwm_fault test` | 78 checks, 0 failures — the escalation and `.noinit` trust rules |
| Press `V` at the receiver while in safe mode | Output returns to Timer1; **a genuine fault drops straight back to failover** |
| Watch `delivered (` on the transmitter log | Delivered duty converges on requested; a persistent gap means the loop is too slow |

## 8. What is already right

Worth stating, because these were the obvious places to get it wrong and they are
correct:

* The split request/read for the DS18B20 conversion (`main.cpp:551-559`) is
  genuinely non-blocking — verified against `DallasTemperature.cpp:473`, not just
  the comment.
* PWM is ISR-driven, so serial blocking, SPI blocking and radio stalls do not
  corrupt the duty output. This is the right architecture and it is why §1.1 and
  §1.3 are the only *heater-affecting* faults.
* The clamp in `pwm_out_set()` (`pwm_out.cpp:76`) is applied before the arithmetic,
  so `permille * 16000` cannot overflow `uint16_t`.
* `sort_by_rom()` correctly uses `i + 1 < count` rather than `i < count - 1`
  (`main.cpp:206`) — the uint8_t underflow is a real trap and it is avoided.
* `frame_crc16()` subtracts `sizeof(uint16_t)` from a size that is never below 2,
  and all five frame types are `static_assert`-ed to the same 13 bytes.
* The ADC is free-running and polled rather than interrupt-driven, correctly
  prioritising PWM edge timing over sampling convenience.
* The CAN `sendMessage` path was deliberately chosen over the previous driver for
  exactly the non-blocking reason §1.3 depends on — and the remediation in §1.3
  makes good on that decision rather than undoing it. Aborting a stuck buffer is
  the right repair precisely *because* send is non-blocking; a blocking driver
  would have needed a different one.
* `can_spi_probe()` reading the register back rather than trusting the write was
  the right instinct all along — it just needed to be told not to run when there
  was nothing to learn.

## 9. Verification of the remediation

### 9.1 Build

Both `CAN_QUIET_RADIO` configurations build clean after every change:

| Configuration | RAM | Flash |
|---|---|---|
| default (`CAN_QUIET_RADIO 0`, `CAN_LISTEN_ONLY 0`) | 979 B (47.8 %) | 17 212 B (56.0 %) |
| `CAN_QUIET_RADIO 1` | 936 B (45.7 %) | 14 944 B (48.6 %) |

Host suites, current: **517 684** checks in `test_eeprom_log`, **74** in
`test_pwm_fault`, zero failures.

### 9.2 Bench results

Run on 2026-10-15 against the current firmware, observed from the radio receiver
— the node's own serial was not available, which is itself the most important
consequence of §4.3 below.

| Property | Result |
|---|---|
| Virgin EEPROM → OFF | confirmed |
| Heartbeat persists the duty (60 s) | confirmed |
| Power-cycle restore at 500 | confirmed, **twice over two restore paths** |
| — via boot marker | confirmed (`src=marker`) |
| — via the window walk | confirmed (`src=window`) |
| Unlocatable ring → OFF + `RESTORE-LOST` | confirmed |
| Live command overrides the fail-safe | confirmed (`src=NRF`) |
| PWM stall → fault record → reboot → recover | confirmed |
| Watchdog fires and recovers | confirmed |
| `X` recovers an anomalous ring | confirmed; the node resumes writing |
| Anomaly → erase → power cycle → 500 | confirmed |
| `PWM-OUTPUT-FAILED` was a phantom | confirmed on hardware |

**Safe mode and `V` revive remain unexercised.** `SYSCMD_PWM_STALL` clears
`TIMSK1`, which a reboot then re-arms through `pwm_out_set()`, so the fault cannot
survive the reset and the escalation never reaches its second stage. The first
stage — record, reboot, recover on the restore point — was confirmed instead.
Closing this needs either a fault that genuinely persists across a reset or a way
to enter safe mode directly, and it is the one gap in the winter path.

### 9.3 Three bugs the bench found that review did not

Worth recording separately, because all three were the same shape: correct in
isolation, wrong in combination.

**The flags byte carried two meanings on bit 5.** `EE_FLAG_PWMFAIL` was `0x20`
and `EE_SRC_MARKER << EE_SRC_SHIFT` is also `0x20`. So every restore from a boot
marker — the common case — reported a PWM fault, and wherever the fault was
genuinely set the source was unreadable, which displayed a normal window-walk
restore as an anomaly. Nine bits were needed into an eight-bit field; three flags
were redundant with data already in the frame and were dropped to make room.
This is what made the flag look like it "kept resurrecting itself" after an erase,
and it wrongly suggested the fault ladder had produced a stall on real hardware.

**The fault flag was a property of the boot, not of the log.** The scan that
derives it lived inside `epl_restore_boot()`, so it ran once and never again.
`X` erased the very record that set the flag and could not clear it, so it
survived until the next power cycle — which is the second half of why it appeared
permanent.

**A 3.4 s command against a 2 s watchdog.** `SYSCMD_EE_ERASE` writes 1024 EEPROM
bytes with nothing feeding the watchdog, so it tripped mid-erase and rebooted into
a half-erased log — the exact state it exists to remove. `SYSCMD_EE_ANOMALY` had
the identical defect and was found by reading, after `X` had already been fixed.
Both now feed every 16 slots.

### 9.4 Open, and not yet closed

| | |
|---|---|
| **Safe mode and revive** | unexercised, and unreachable from the bench (§9.2) |
| **CAN RX overrun under EEPROM writes** | `RXBUF0-OVR` appeared during both `X` and `A`, then cleared. The node does not service CAN while blocked writing, so the controller's RX buffer overruns. It is §4.3's starvation showing under load, not a bus fault — but it is unquantified. |
| **No transmitter-side diagnostics** | Every result above was read off the radio, because the node's serial was not reachable. `win_frames`/`win_cmds`/`win_bad` are not carried on any frame, so "command arrived and was rejected" is indistinguishable from "command never arrived". That cost several wrong conclusions during this session. |
| MCP2515 TX behaviour (§1.3) | still from datasheet semantics and the `autowp` source, not measured |
