# Auto Heating — Implementation TODO List

**Source:** `whitepaper_auto_heating.md` (rev 1.2, 2026-10-23)  
**Status:** Phases 0–7 are in the tree (commits after `1759c78`). The gaps below block the §13 AUTO release.  
**Scope:** `ESP32/28_hauszentrale` + Wolf gas boiler (`AVR/01_heizung_temp`)

---

## Gaps in the current tree (review after `1759c78`)

The checked boxes above mean the function exists and a host test calls it. These items are what the firmware that runs still gets wrong. A first boot with no `/heater.bin` writes mode OFF and does not transmit. Selecting AUTO commands the boiler.

### [ ] G.1 Wire boot transmit into the controller that runs
- `heater_handle_boot_restore()` is host-tested. `main.cpp` never calls it
- OFF does not send 1000 when `0x280` is still a heating duty. The page can show OFF while the boiler keeps the previous duty, including after an AVR power cut inside the 60 s EEPROM window
- MANUAL does not resend `saved_duty` when the report differs
- AUTO already waits for the first legal decision inside `heater_ctl_tick`. That part is live
- The 1-permille "lowest-permille walkback" in `heater_handle_boot_restore()` is not the §5.2 rule. §5.2 describes the AVR's own EEPROM window. Do not wire that walkback as written
- See §5.2

### [ ] G.2 Make the HEATER page tell the truth
- `src/ui/haus_ui.c` still says "AUTO is shadow: it does not send yet."
- `heater_ctl_tick` sends about one second after AUTO is selected
- The page runs its own `heater_schedule_compute()` with frost forced off. It does not read `heater_ctl_get()`, so it cannot show a suppressed start, a duty step, a failed send, or the yield timer
- The two text lines should come from that snapshot: what (day or night, Soll, asked duty) and why (cycle result, duty on the wire, the one timer blocking the next frame)
- `PUMP BLOCKED` and `FROST` badges have no live source
- See §8, §13

### [ ] G.3 Keep a shadow mode that does not transmit
- Phase 3's `heater_shadow_tick` is gone. Phase 4 transmits
- §9 requires one schedule edge, or a few hours of logs, to be reviewed before AUTO may command the boiler
- The next flash needs a mode that logs the decision and does not call `gui_send_pwm()`
- See §7.5, §9

### [ ] G.4 Yield when `0x280` shows a duty this panel did not send
- The live path enters `MODE_OVERRIDE` only for `PWM_SRC_NRF`
- Any other writer is logged once and can be overwritten on the next schedule step
- The tested `heater_handle_foreign_writer()` is not called from `main.cpp`
- See §5.1, §5.2

### [ ] G.5 Repeat a frame that `gui_send_pwm()` did not accept
- `heater_cycle_step()` advances, then `ctl_send()` logs the failure and leaves the cycle as if the frame went out
- That duty is not sent again until the target changes
- A conflicting `0x280` stays log-once (§5.9). A failed transmit is a different case
- See §5.9

### [ ] G.6 Decide the first AUTO step
- The cycle starts idle with last duty 1000 and ignores the duty already on `0x280`
- The first `SEND` is the full day or night duty (283 or 500), not a 50-permille walk from the wire
- The 50-permille limit in §5.6 applies while `HEATING`. Confirm whether the first edge must step as well
- See §5.6

### [ ] G.7 Frost demand never runs
- `heater_ctl_tick` passes `frost_active = false`
- The threshold is still an open number. Do not invent it
- Manual OFF remains the only real off. Night is duty 500, not 1000
- See §5.3, §5.4

### [ ] G.8 Finish the host tests §7.1 actually requires
- `test/test_heater_control/Makefile` has no `make test` or `make sanitize` target. The binary passes when built by hand
- A sanitizer run prints undefined behavior in `payload_from_config()` (`haus_heater_bin.cpp`) when the rejection test stores `(HeaterMode)(HEATER_MODE_OVERRIDE + 1)`, and the process still exits 0
- Case 4 ("24 simulated hours") and case 17 ("seven-day simulation") call the unused authority API a handful of times. They do not drive `heater_ctl_tick` across that time
- Case 12 checks two hours on 20 Oct 2026. The spring-gap hold and the fall-back hold are not tested
- Local time is a hand-rolled `mktime` conversion in `haus_heater.cpp`, not `localtime` under `CET-1CEST,M3.5.0/2:00,M10.5.0/3:00`
- T.2, T.3, and the T.4 images are still open
- §13 still needs the bench shadow review and the operation-window note
- See §7.1, §13

---

## Phase 0 — Core Control Infrastructure

### [x] 0.1 Define `HeaterMode` enum
- File: `include/haus_heater.h` (created)
- Enum values: `HEATER_MODE_OFF`, `HEATER_MODE_MANUAL`, `HEATER_MODE_AUTO`, `HEATER_MODE_OVERRIDE`
- See §5.1

### [x] 0.2 Implement inverse table (`OffTableModel`)
- File: `src/haus_water_model.cpp` (extend existing)
- Five knots: duty 100→70 °C, 200→60 °C, 300→54 °C, 400→44 °C, 500→42 °C
- `target_from_duty()`: interpolate Soll from duty
- `estimate()`: returns false for duty < 100 or > 500 (including 0 and 1000)
- Domain: Soll 42…70 °C ↔ duty 500…100
- Day 55 °C → duty 283 (interpolated between 60 °C/200 and 54 °C/300)
- Night floor at 42 °C → duty 500 (bypasses inverse — OFF is duty 1000, never consults model)
- Duty 0 stays manual-only
- Host test: `test/test_heater_control/test_heater_control.cpp` (test case T.1 #13)
- See §5.3

### [x] 0.3 Implement scheduled flow law
- Day flow: default 55 °C → duty 283
- Night flow: `T_day - 15 K`, floored at 42 °C → duty 500
- Real off: duty = 1000 (manual OFF, schedule-off, or economy lockout)
- Frost demand: modest point on adopted table, independent of schedule
- 15 K setback = AWT's recommended flow setback (not room)
- Fixed preheat lead (60–90 min) — deferred until house time constant known
- Schedule edge means "heating starts", not "room is warm"
- See §5.3

### [x] 0.4 Implement authority/boot/foreign-writer handling
- Manual OFF while AUTO → `MODE_OFF`, send 1000 immediately, persist, cancel AUTO
- Deliberate manual duty while AUTO → `MODE_MANUAL`, send user value once, persist
- AUTO control → enter `MODE_AUTO` only when CAN is green and inputs are valid
- Foreign `0x280` / nRF change → `MODE_OVERRIDE` for bounded time (2 h), then return to AUTO
- Reboot → restore mode from `/heater.bin`; read `0x280`; send only when it disagrees
- Sensor loss → hold, then `AUTO HOLD`; do not silently switch modes
- Unsynced clock → hold; do not apply day flow until `CanState.synced`
- Schedule/sensor recovery cannot override `MODE_OFF`
- These transitions live in `heater_handle_*`. The running controller does not call them. See G.1 and G.4
- See §5.1

### [x] 0.5 Implement boot transmit rules
- Panel sends only when `0x280` disagrees with restored mode:
  - OFF + report is 1000: send nothing
  - OFF + report is heating duty: send 1000 once
  - MANUAL: send saved duty only when report differs
  - AUTO: send nothing until first legal decision; show `PWM_SRC_EEPROM`
- Handle `PWM_SRC_NRF` and foreign `0x280` → yield, don't overwrite remote
- Handle EEPROM heartbeat edge cases (60 s write window, lowest-permille walkback)
  - 60 s write window: if last EEPROM write was < 60 s ago, don't re-transmit
  - Lowest-permille walkback: when restoring a lower duty, step down at most 1
    permille per write (prevents sudden large drops)
- The function is not on the live path, and the 1-permille walkback is not §5.2. See G.1
- See §5.2

---

## Phase 1 — Config Persistence and UI Mode

### [x] 1.1 Implement `/heater.bin` persistence
- Versioned binary: magic, version, CRC32 over payload
- Pattern: follow `day-file`/`rollup.bin` (not `sensor_roles.bin` which has no CRC)
- On bad file: reject, keep previous mode in RAM, never come up as AUTO
- Persist: mode, schedule (day flow, night setback, frost parameters)
- Also persists `saved_duty` (needed by the §5.2 MANUAL boot rule)
- Version 2 file is 36 bytes: 10-byte header (magic/version/CRC32) + 26-byte payload. A version-1 file is rejected
- File: `include/haus_heater_bin.h`, `src/haus_heater_bin.cpp`
- Pure encode/decode split from SPIFFS I/O so host tests can run without flash
- Corrupt/absent file: defaults (`OFF`) are written, so the next boot is safe
- Host tests: round trip, size/layout, too-small buffer, bad magic/version/CRC,
  out-of-range mode/hour, truncated buffer (decode leaves state unchanged)
- See §8

### [x] 1.2 Add AUTO mode control to HEATER detail page
- Mode selector: `[ OFF | MANUAL | AUTO ]`
- Display: requested duty, in-force duty, flow range (e.g., "AUTO heating, flow 55 → 42 °C")
- Takeover is deliberate: mode segment toggle, or slider **release** (not drag)
- While AUTO selected, slider displays commanded duty, does not write the bus
- A drag that started on AUTO screen does not change mode at `VALUE_CHANGED`
- AUTO disabled when CAN not green or inputs invalid (show reason)
- Badges: `AUTO`, `AUTO HOLD`, `PUMP BLOCKED`, `OVERRIDE`, `NIGHT`, `FROST`, `off — frost protection inactive`
- `PWM_SRC_EEPROM` shown while wire carries AVR's restored duty
- Request full redraw after writes (as `haus_config_set_can_enabled()` does)
- Implemented in `src/ui/haus_ui.c` (`pwm_*`), config cache in `haus_heater_bin`
- The page still says AUTO is shadow and never transmits. The controller does transmit. See G.2 and G.3
- Switch = manual OFF/ON: OFF persists `MODE_OFF` and sends 1000; ON sends the
  saved manual duty once and persists `MODE_MANUAL`
- Slider sends on `LV_EVENT_RELEASED` only, so a drag cannot produce a burst
- AUTO segment disabled (dimmed) while CAN is not green or the clock is unsynced
- `FROST`/`PUMP BLOCKED` badges exist and still have no live source. See G.2 and G.7
- Host tests cover the config cache; UI render variants are T.4
- See §8

---

## Phase 2 — Anti-Short-Cycling

### [x] 2.1 Implement demand-edge state machine
- States: `IDLE → HEATING → COOLDOWN → IDLE`
- Pure state machine in `haus_heater` (`heater_cycle_*`), host-tested
- Parameters (all persisted and configurable):
  - Control period: 60 s
  - Minimum on time: 10 min
  - Minimum off time: 15 min
  - Maximum demand edges per rolling hour: 2
  - Minimum interval between duty changes: 5 min
  - Maximum duty step: 50 permille
  - Future room hysteresis (trim only): ±0.3 K
  - Future flow deadband (trim only): 2 K
- Rules:
  - OFF sent only after `min_on` (except manual OFF and pump guard)
  - ON sent only after `min_off` and within edge budget
  - Duty may move while HEATING; only on/off transitions are limited
  - Frost protection may override `min_off` and edge budget
  - Manual OFF beats frost protection
  - Budget counts **AUTO demand edges**, not burner ignitions
  - Guarantee explicitly scoped to AUTO output (boiler's `Taktsperre` still applies)
- Parameters live in `HeaterConfig.cycle`, persisted in `/heater.bin` (format v2)
- `heater_cycle_step()` returns `SEND` with one duty, or the suppression reason
  (`MIN_ON`, `MIN_OFF`, `EDGE_BUDGET`, `DUTY_INTERVAL`)
- On-edges are timestamped in a rolling 1 h window (`HEATER_EDGE_HISTORY` = 8)
- `heater_cycle_force_off()` records an external off-edge (manual OFF /
  OVERRIDE / pump guard) without clearing the rolling budget
- Host tests cover min_on, min_off, budget, rolling expiry, duty stepping,
  frost override, and forced off
- `heater_ctl_tick` consumes it and can transmit. The first edge does not step from the wire. See G.5 and G.6
- See §5.6

---

## Phase 3 — Short Shadow Mode

### [x] 3.1 Implement shadow mode (compute and log, do not transmit)
- Before AUTO is allowed to transmit
- One schedule edge, or a few hours (not a multi-day gate)
- Log: decision and reason, suppressed reason (`min_on`, unsynced, NRF yield, `AUTO HOLD`), flow and room traces, planned duty inside `100..500` or `1000`, switching rate
- Enable transmit after short review
- Implemented in `src/main.cpp` (`heater_shadow_tick`), called at 1 Hz but
  gated to `control_period_s` (60 s)
- Log tag `heater_shadow`: `cycle`, `sched`, `decision`, `req`, `planned`,
  `edges/h`, `water_in`, `water_out`, `room`, `soll`
- Holds logged with a reason: `can-not-green`, `clock-unsynced`,
  `schedule`, `nrf` (yield)
- Superseded on the live path: `heater_shadow_tick` is gone and `heater_ctl_tick` calls `gui_send_pwm()`. See G.3
- Mode changes are logged once; entering OFF records a forced off-edge so a
  later AUTO respects `min_off`
- No multi-day gate: review one schedule edge or a few hours of lines
- See §7.5

---

## Phase 4 — Enable AUTO

### [x] 4.1 Enable AUTO transmission with adopted defaults
- Deliberate takeover tested
- See §9, step 4
- Implemented in `haus_heater_ctl` (`heater_ctl_tick`), wired from `main.cpp`
- AUTO now sends through `gui_send_pwm()` only on a `SEND` decision; the
  schedule, edge budget, and duty stepping are unchanged
- Manual takeover (switch/segment/slider release) still writes the bus from
  the UI and changes the persisted mode, which stops AUTO on the next tick
- Host tests cover the first on-edge, control-period gating, and last sent duty

### [x] 4.2 Implement failure handling
- SHT45 stale → scheduled flow continues; if trim active, hold. Badge `AUTO HOLD`
- Water sensors stale → pipe model leaves winter fit; scheduled flow continues on adopted table
- Clock unsynced → hold restored duty; do not apply day flow
- CAN not green → AUTO unavailable, manual controls disabled
- `0x280` conflicts with request → log once, show actual, do not retry-loop
- Command outside legal range → reject, log, hold
- Foreign writer / nRF → `MODE_OVERRIDE` for bounded time, then return to AUTO
- Boiler fault or DHW charge → log as disturbance; do not fit as plant gain
- No undefined open-loop fallback; fallback is HOLD + reason
- See §5.9
- `heater_ctl` holds (no frame) on CAN not green / clock unsynced; the cycle
  does not advance while held
- nRF source → `MODE_OVERRIDE` for 2 h; the cycle gets a forced off-edge on
  entry, then AUTO resumes (no blind re-send at expiry)
- `0x280` mismatch is logged once (`expected`/`reported`) and is not retried. A failed `gui_send_pwm()` is also not retried. See G.4 and G.5
- Boiler fault on `0x280` is logged once as a disturbance; DHW charge is not
  visible on the current CAN set
- Room/pipe sensors are not preconditions: scheduled flow needs neither
- UI already disables manual controls when CAN is not green
- Host tests cover clock hold, CAN hold, override entry/expiry, and resume

---

## Phase 5 — Operation Window

### [x] 5.1 Implement experiment logger (1 Hz, monotonic timestamps)
- CSV format: `epoch_ms,seq,duty_req,duty_rpt,water_in_c,water_out_c,room_c,room_rh,ac_rms_dc,ac_angle_cdeg,pump_state,can_ok,target_c,note`
- `duty_req` = what panel sent; `duty_rpt` = what `0x280` reported (both mandatory)
- `water_in_c`, `water_out_c` = pipe-wall temperatures (not bulk water)
- `target_c` = Soll hint from table (never a pipe value)
- `pump_state` = from coil, not duty-derived label
- Store experiment files in `experiments/<id>/` (not in minute record)
- Implemented in `haus_heater_log`; path
  `/experiments/heater_<epoch-or-boot>/trace.csv`
- One header line, then one row per second; `epoch_ms` is unix ms when synced,
  boot ms when unsynced
- Manual UI sends are recorded through `heater_ctl_note_sent()`, so `duty_req`
  includes manual switch/slider frames
- `pump_state` is coil-only (`1` when `ac_fresh` and RMS > 0, `-1` stale); the
  calibrated threshold arrives with Phase 6
- SD absent: nothing written, start retried every 60 s (no SPIFFS fallback)
- `note` carries mode/cycle/hold/result/block/override without commas
- See §6.3

---

## Phase 6 — Pump Guard (Later)

### [x] 6.1 Implement pump-current guard
- Input: `CanState.ac_rms_dc` (0x2C waveform RMS in 0.1 ADC count)
- Trigger: request heat + coil count < I_min for 10 min + ΔT < 2 K → send 1000, state `PUMP_BLOCKED`
- Recovery: stay at 1000 for 30 min, then allow one supervised demand
- Clear early if room has fallen further (valve opened)
- Ignore guard while coil sample is stale
- Record installed pump-switch position first
- Must not use "current high" as clear condition
- 3-minute grace = `Taktsperre`
- Equipment protection only (suspends AUTO; manual OFF/duty untouched)
- Implemented in `haus_heater_ctl` (`pump_guard_update`) with
  `HeaterPumpParams`: grace 180 s, confirm 600 s, hold 1800 s, ΔT 200 c°
- **Disabled by default** (`HeaterPumpParams.enabled = false`): the installed
  pump-switch position, coil `I_min`, τ and α are still missing, so enabling a
  made-up threshold would be a false-trip risk
- The guard never clears on "current high"; recovery is the 30 min timer or a
  0.5 K further room drop
- Latch sends 1000 once, forces an off-edge, and suspends only AUTO; a manual
  mode change clears the latch and leaves manual frames untouched
- Host tests cover grace, confirm, latch send, and the 30 min recovery
- Remaining field work: record the switch position and coil threshold, then
  call `heater_ctl_set_pump_params()` with `enabled = true`
- See §5.7

---

## Phase 7 — HTTP Observability

### [x] 7.1 Implement `/heater` JSON endpoint
- Fields: mode, state, setpoints, requested duty, reason, remaining minimum times, edge count
- See §9, step 7
- Implemented in `main.cpp`; reads the `heater_ctl` snapshot
- Includes `mode`, `state`, `cycle`, `hold`, `reason`, `can_green`,
  `clock_synced`, `override`(+remaining), `pump_blocked`(+remaining)
- `setpoints`: day, night flow, night floor in °C
- `requested_duty` (schedule), `planned_duty` (after cycle limits),
  `last_sent_duty`, `duty_reported` (`-1` when no 0x280 yet)
- `sched_reason`, `result`, `min_on_remaining_s`, `min_off_remaining_s`,
  `duty_interval_remaining_s`, `edges_last_hour`
- `trace` is the current SD experiment CSV path, or `""`

---

## Phase 8 — Winter Iteration

### [ ] 8.1 Implement experiment and model lab tools
- Location: `tools/heater_model/`
- Components:
  - `collect.py` — read operation logs over HTTP/SD or external logger CSV
  - `experiment.py` — optional supervised steps; refuses unless `--allow-write`
  - `identify.py` — pipe model (§5.8) fitting, then regime detection
  - `llm_analyst.py` — sends downsampled traces to LLM; produces hypotheses and experiment proposals (never actuator commands)
  - `report.py` — generates `report.md`, plots, and `model.json`
  - `model.md` — model card: inputs, states, parameters, confidence, validity range
- See §6.2

### [ ] 8.2 Implement pipe model fitting (§5.8)
- Two relations, kept apart:
  - `τ · dT_pipe_out / dt = T_inf − T_pipe_out`
  - `T_inf = T_pipe_in + α · (T_soll − T_pipe_in)`
- τ is identified (not the boiler's 100 s)
- α lies between 0 and 1 (identified; α = 1 is a result, not a constraint)
- When α < 1, steady pipe reading stays below Soll — that is the model, not an error
- Output: `model.json` (τ, α, any load term, uncertainty, validity range)
- The five duty knots are recorded as adopted inputs, not fitted outputs
- See §6.5, §6.7

### [ ] 8.3 Implement regime detection
- Segment: OFF, pump-only, ignition wait, weak fire, strong fire, modulation, lockout
- Start with thresholds on coil RMS and slope of flow-pipe reading
- Only then consider a hidden Markov model
- See §6.5

### [ ] 8.4 Implement winter iteration loop
- Each pass: split log into day/night/off; drop intervals where `duty_req` ≠ `duty_rpt`
- On intervals that held one duty long enough to settle, record median pipe-out, pipe-in, coil RMS
- Fit §5.8 model
- Outcomes (only one): unchanged / pipe-model note / load term / new knot / boiler visit
- See §6.4

---

## Phase 9 — Room Trim and Outdoor Economy (Future)

### [ ] 9.1 Implement room-temperature trim (not v1)
- Compare SHT45 with a second thermometer (panel sits on H1, living room, next to un-dimmable backlight)
- Add at most a few kelvin of flow trim from room error
- `too cold = T_room < T_set - 0.3 K`
- `satisfied = T_room > T_set + 0.3 K`
- Requirements:
  - Radiator valves in panel's room fully open while trim active
  - Trim is clamped; integrator frozen when transmitted duty does not change
  - One room sensor does not become the house thermostat
  - `T_flow_set` comes from schedule, not from an integrator running during off state
- See §5.5

### [ ] 9.2 Implement outdoor economy lockout (future)
- No `0x180` probe will be assigned to outdoor weather
- A later summer switch may use an internet forecast. A missing or old forecast leaves the schedule running and does not set the flow temperature
- Look for the old AF before buying a probe, if a local sensor is still wanted
- AWT's summer switch: pump and burner off above adjustable outdoor temperature (recommended 15–20 °C)
- Preferred energy feature before any room trim
- See §3.3, §11 decision #12

---

## Optional — Boiler Visit (Deferred)

### [ ] B.1 Conduct supervised boiler visit (only when triggered)
- Triggers:
  - Pipe model has τ, long wait against τ passes, flow-pipe reading still has no slope
  - Coil says pump never started
  - Duty 100 is not enough heat and someone wants a point above 70 °C
- Procedure:
  1. Write down voltmeter method, node measured, and load
  2. Set boiler flow-temperature selector to documented position
  3. At each chosen duty, record: duty request/report, boiler's own heating-temperature display, measured control voltage, both pipe readings, room temperature, pump/coil state
  4. Repeat after boiler has settled (not immediately after step)
  5. Mark any DHW charge or boiler fault
- A display reading is the only observation that may change a knot (written by hand)
- A pipe reading never justifies a knot change
- Duty 0 stays manual-only until it has a point
- See §4.4

---

## Test Environment (All Levels)

### [ ] T.1 Level 1: Pure controller tests
- File: `test/test_heater_control/`
- Flags: `-std=c++17 -Wall -Wextra -Wconversion -Wshadow -Werror`
- Commands: `make test`, `make sanitize` (address/undefined sanitizers)
- 17 required test cases:
  1. Night onset — flow floors at 42 °C, duty 500; no off edge
  2. Day transition — flow returns to day value, subject to mover
  3. Real off / economy — OFF after `min_on`; at most 2 demand edges per rolling hour
  4. Manual OFF during AUTO — 1000, mode OFF, no AUTO sends for 24 simulated hours
  5. Deliberate manual takeover — Mode MANUAL, one user frame, AUTO cancelled
  6. AUTO re-entry — No send until preconditions valid
  7. Missing SHT45 in v1 — Scheduled flow continues; trim, if active, holds
  8. Unsynced clock — Hold restored duty; no day-flow jump
  9. Pump blocked — OFF latch after debounce; recovery by 30 min timer or room drop
  10. Coil stale — Guard inactive, AUTO continues
  11. Frost demand — Modest flow despite night; manual OFF still wins
  12. DST/weekend schedule — Correct local-time evaluation, fall-back and spring-gap hold
  13. Adopted inverse — Round trip on five knots; day 55 °C → duty 283; night floor → duty 500; OFF is 1000; AUTO never emits duty 0
  14. Command domain — Always 1000 or 100..500
  15. Foreign writer — `MODE_OVERRIDE`, then bounded return to AUTO
  16. Boot with restored duty — No transmit when `0x280` matches mode; `PWM_SRC_EEPROM` visible
  17. Seven-day simulation — Bounded demand edges, no invariant violation
- See §7.1

### [ ] T.2 Level 2: Closed-loop simulator
- File: `host_harness/src/heater_sim.cpp`
- Model: pipe-wall lag (§5.8) with α < 1, boiler hysteresis, 3-min ignition delay, optional load disturbance
- Keep for state-machine invariants
- See §7.2

### [ ] T.3 Level 3: Replay
- Replay `dump_2026-10-23/sd/data_*.bin` and `rollup.bin` through existing dump loader
- Explicit mode/duty injection
- Drives plant side; cannot score AUTO decision
- See §7.3

### [ ] T.4 Level 4: UI render tests
- Generate reference images:
  - `output_heater_auto.png`
  - `output_heater_night.png`
  - `output_heater_hold.png`
  - `output_heater_pump_blocked.png`
  - `output_heater_manual.png`
  - `output_heater_frost_off.png`
- See §7.4

---

## Housekeeping / Documentation

### [ ] D.1 Update parent whitepaper (`docs/whitepaper.md`) in three places
- Duty direction (§7.9 and D27): correct to match S20 bench (`1000` = OFF, `0` = full heat)
- Transmit gesture (F19 / D28): detail page transmits on switch click and slider tick
- Water out (§7.11 and Q9): `haus_sensor_roles.cpp` seeds third ROM as `SENSOR_FN_WATER_OUT`; table is Soll hint, not water-out temperature
- See §3.5

### [ ] D.2 Verify DS18B20 probe identities via live `0x180`
- Confirm both DS18B20s are clamped to pipes (not immersed)
- Confirm `water_in` and `water_out` roles
- Note: a missing probe does not block AUTO (scheduled flow does not need pipes)
- See §5.8, §11 decision #4

### [ ] D.3 Document pipe-wall sensor limitations
- Both DS18B20s read pipe wall, not bulk water
- Pipe reading can settle short of water temperature (contact + DS18B20 body slower than boiler's 100 s NTC)
- A pipe finishing below Soll is the sensor, not a missed setpoint
- See §5.8

### [ ] D.4 Create model card template
- Inputs, states, parameters, confidence, validity range
- See §6.2 (`model.md`)

---

## Summary: Integration Order (from §9)

| Step | Task | Phase |
|------|------|-------|
| 1 | Inverse of off-table v1 + host tests | Phase 0 + T.1 |
| 2 | Config persistence + UI mode | Phase 1 |
| 3 | Short shadow (compute, log, do not transmit) | Phase 3, then G.3 |
| 3a | Fix the live-path gaps before AUTO may command the boiler | G.1–G.8 |
| 4 | Enable AUTO with adopted defaults | Phase 4, after G.3 |
| 5 | Operation window (few days) | Phase 5 |
| 6 | Pump guard (after τ and α exist) | Phase 6 |
| 7 | HTTP observability (`/heater` JSON) | Phase 7 |
| 8 | Winter iteration (pipe-model note) | Phase 8 |
| 9 | Room trim + outdoor economy | Phase 9 |
