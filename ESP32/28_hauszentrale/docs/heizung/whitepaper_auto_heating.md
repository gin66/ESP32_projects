# Automatic Heating Control for 28_hauszentrale

**Status:** design proposal, not implemented  
**Revision:** 1.2, 2026-10-23 — adopts the existing duty table, defers any
boiler visit, and models the two DS18B20s as pipe-wall sensors. The
measurement-gate review was removed once this revision absorbed it.  
**Scope:** `ESP32/28_hauszentrale` and the Wolf gas boiler installation served by
`AVR/01_heizung_temp`  
**Related:** [`../whitepaper.md`](../whitepaper.md) §7.9, §7.11; N15; S20  
**Sources:** [`guegu1e.pdf`](guegu1e.pdf) (Wolf GU-E/GU-1E/GG-E/GG-1E planning and
installation manual), [`3044603_0599_AWT1_analoger_witterungsgefuehrter_Temperaturregler.pdf`](3044603_0599_AWT1_analoger_witterungsgefuehrter_Temperaturregler.pdf)
(Wolf AWT 1 analog weather-compensated controller), the 2026-10-18 S20 bench
notes, the owner's one-time voltage campaign, and the owner's decision of
2026-10-23 to run AUTO from the existing table and to treat the DS18B20
readings as pipe-wall temperatures.

**Revision history**

| Rev | Date | Change |
| --- | --- | --- |
| 1.0 | 2026-10-23 | First design proposal |
| 1.1 | 2026-10-23 | Processed review: one demand law, scheduled flow instead of room PI, corrected start-budget and night/frost logic, pump-latch recovery, ΔT sign, boot/NRF/unsynced behaviour, deliberate takeover, CRC persistence, model-lab order, test cases and decisions. The review file was removed after this text was checked against it |
| 1.2 | 2026-10-23 | No boiler visit before AUTO. Off-table v1 is the adopted Soll inverse. A few days of operation, then a winter pipe-model loop. Both DS18B20s read the pipe wall; water-out is slow and may settle below Soll. That gap does not edit the duty knots. The measurement-gate review was removed after this text was checked against it |

---

## 1. Executive summary

The panel already has a manual actuator path: one CAN duty command (`0x240`),
rendered back as `0x280`. The next step is an automatic mode with:

- a day/night flow-temperature schedule (Nachtabsenkung),
- guaranteed minimum on/off times and a bounded number of **AUTO demand
  edges**,
- a later pump/thermostat guard,
- an explicit AUTO mode beside the existing manual switch and slider,
- manual OFF and manual settings that always remain available and always win.

The central technical finding of this paper is that **the boiler is not a
linear duty-to-temperature actuator**. It contains its own controller, burner
modulation, pump logic, thermal hysteresis and a start delay. The one-time
voltage campaign shows the same input producing different boiler states
depending on the current water temperature and the previous state.

Therefore:

1. The first automatic release is a **scheduled-flow controller**, not a room
   PI. It reproduces the intent of the replaced AWT: two flow setpoints
   (day/night), a real off, and a frost demand.
2. The existing off-table v1 (`100 → 70 °C` through `500 → 42 °C`) is the
   **adopted Soll inverse** for v1. It is a sparse steady-state hint, not a
   confirmed boiler-display map and not a plant model. Duty 0 stays
   manual-only. A visit to read the boiler display is a later branch, not a
   precondition.
3. After a few days of AUTO operation, a winter loop fits a **pipe-wall
   model** of water-in, Soll and water-out. Both DS18B20s are clamped to the
   pipe, not immersed. Water-out follows slowly and may settle below Soll.
   That fit does not edit the duty knots. The lab uses deterministic system
   identification; an LLM is an analyst and experiment planner, not part of
   the control loop. A control-voltage measurement is deferred with the
   boiler visit.
4. The guarantee "no on/off within minutes" is a property of **AUTO's own
   demand edges**. Manual OFF, the nRF remote, another CAN writer, and the
   boiler's own `Taktsperre` can still change the physical state. The panel must
   say so.
5. Manual OFF is latched and absolute. Because that removes the AWT's
   AWT-style frost behaviour, the OFF badge must state that frost protection is
   inactive.

---

## 2. Scope and non-goals

### 2.1 In scope

- Automatic heating mode, selectable with an AUTO control.
- Day/night flow-temperature schedule and Nachtabsenkung.
- Anti-short-cycling of AUTO demand edges.
- Handling of missing or stale inputs, unsynced clock, and foreign writers.
- Later pump-current guard: if all thermostats are closed, stop the call for
  heat.
- A host test environment with deterministic use cases.
- A data-analysis tool for operation logs: duty, the two pipe readings, room
  and coil. A control-voltage channel is deferred.

### 2.2 Out of scope

- Replacing the boiler's safety functions.
- Controlling domestic hot water (DHW). A storage charge is a disturbance that
  must be marked in experiments.
- Room-temperature feedback in v1. It is a later, bounded trim.
- Weather compensation using an outdoor sensor in v1. The old AWT had one; the
  panel currently has none. It is the preferred later addition.
- Automatic flashing, automatic experiment execution on the live heater, or
  any LLM-generated actuator command.

### 2.3 Terminology

| Term | Meaning |
| --- | --- |
| **Duty** | `duty_permille` in `0x240`/`0x280`. `1000` is OFF, `100..500` is the panel's usable heating range. Lower duty = higher control voltage = more heat. |
| **Soll** | Boiler flow-temperature target, in °C. The temperature the boiler is asked to make the water. The panel derives the hint from duty through the adopted table. It is not a sensor reading. |
| **Pipe reading** | A DS18B20 clamped to the outside of a pipe. `water_in` and `water_out` in the log are pipe-wall temperatures. The panel never sees the bulk water. |
| **AUTO** | The new control mode. |
| **MANUAL** | User-selected duty. |
| **OFF** | Latched user intent: no automatic heating. |
| **Demand edge** | A transition of AUTO's logical demand from off to on, or on to off. It is not a burner ignition. |
| **Adopted table** | Off-table v1: the quasi-steady hint `duty → Soll` used by v1. Domain Soll 42…70 °C ↔ duty 500…100. Adopted, not confirmed on the boiler display. |

---

## 3. The existing system

### 3.1 Panel and actuator path

- The PWM switch and slider are built by `pwm_control_build()` in
  `src/ui/haus_ui.c`.
- `gui_send_pwm()` (`src/haus_gui.cpp`) is the only project path to
  `haus_can_bus_send_pwm()`.
- The legal values are `0..500` and `1000`. The project deliberately does not
  send `501..999`.
- `1000` is OFF. In the ON range, `0` is "full heat" and `500` is "minimum
  heat" on the UI; the measured bench table is `100→70 °C … 500→42 °C`.
- The PC817 interface inverts the duty. The measured control voltage was
  `0.41 V` at `1000` and `11.28 V` at `100`, nonlinear in between
  (`docs/todos.md`, S20.02, bench 2026-10-18).
- The panel is **not** the safety interlock (N15). The AVR owns output safety,
  minimum pulses, ramping and dead-man behaviour. This paper does not change
  that.
- `gui_send_pwm()` is the only caller of `haus_can_bus_send_pwm()`. The
  symbols are the durable references; line numbers will rot.

### 3.2 The boiler has its own behaviour

The GU-E/GU-1E/GG-E/GG-1E manual is the only authoritative source for the
installed boiler family. The points that matter for control are:

| Property | Value / behaviour | Source |
| --- | --- | --- |
| Heating circuit sensor | NTC in the flow, time constant about 100 s | manual p5 |
| Flow temperature range | up to about 90 °C | manual p5 |
| Modulating range | e.g. 8.0–18 kW or 10.9–24 kW depending on type | manual p5 |
| Burner start delay | about 3 minutes (`Taktsperre`) | manual p21 |
| Pump mode | continuous, or "only on heat demand"; optional 40 s pump run-on | manual p21 |
| Night setback warning | cancel night setback below −12 °C outdoor | manual p21 |
| Analog controller | Wolf continuous or weather-compensated controller on RT/WT/EWT | manual p21 |
| Flow-temperature selector | must be set to "9" when an external continuous controller is used | AWT 1 p3 |
| Bridge | remove the bridge between the controller input and 24 V | AWT 1 p3 |
| DHW | a connected `Speicher` is diverted by the three-way valve; a charge looks like a sudden flow/return/burner change at constant panel duty | manual p21 |
| Programme switch | heating, otherwise the appliance does hot water only | AWT 1 p3 |

The important consequence is that a voltage step is a **demand** into a
stateful controller. It is not a direct valve position and not a direct flow
temperature. The boiler can cycle internally while the panel duty is constant,
and minimum modulation means it will do so at low load regardless of the panel's
minimum on time.

The boiler text also says only a Wolf accessory controller may be connected to
RT/WT/EWT. The PC817 path is already outside that sentence. AUTO does not make
it more so. A boiler fault or a storage-charge cycle is a disturbance to log,
not plant gain.

### 3.3 The replaced AWT 1 controller

The AWT 1 is an analog weather-compensated controller with a daily timer. Its
documented behaviour:

- Outside sensor `AF` on a north/northeast wall.
- Control output on a 24 V DC interface (`MAX. 24 VDC/20 mA`).
- Heating-curve slope (`Steilheit`) selectable, recommended 0.7 or 1.2 for
  70/50 °C and 50/40 °C systems.
- Temperature correction in 5 °C steps, from −20 °C to +20 °C.
- Night setback (`Absenkung`) from 0 to 40 K in 5 K steps, applied to the
  **flow** temperature — not to a room setpoint.
- Economy automation switches pump and burner off above an adjustable outdoor
  temperature, recommended 15–20 °C.
- Daily switching timer with heating / setback / off programmes.
- Recommended setting table in AWT 1 p4: setback 10–15 K, slope 0.7–1.2,
  economy automation 15–20 °C, temperature choice Standard or +1.

The AWT is **open loop**: it may be mounted on any interior wall; the outdoor
sensor is mandatory; it does not close a loop on room temperature. The panel's
v1 should reproduce that structure before adding room trim.

Room control has an installation rule the AWT avoided. Boiler manual p30: if a
room-temperature controller is used, every radiator valve **in that room** must
be fully open, and the controller must not be behind furniture or a curtain. A
single SHT45 on the panel is not a house thermostat.

### 3.4 Interface physics

The panel's path is:

```
duty_permille ──0x240──► AVR ──PWM──► PC817 ──control voltage──► boiler
                                                    │
                                                    └── internal controller,
                                                        pump, burner, hysteresis
```

The control voltage is not measured by the panel. The panel knows only the
requested duty and what `0x280` reports. The measurement campaign below used a
separate voltage observation; that observation is not available continuously
on the panel today.

**Open instrumentation question:** add a voltage-sense channel to the panel, or
record the control voltage with an external logger during a later visit.
Without one of these, the lab can fit only `duty → pipe reading`, not
`duty → voltage → boiler state`. That chain is not a v1 blocker. The adopted
table already supplies the Soll hint, and the pipe model in §5.8 is what the
winter loop identifies.

### 3.5 Parent whitepaper is stale in three places

Implementing AUTO by "following `docs/whitepaper.md`" will undo the bench.

| Topic | Parent whitepaper | Firmware and this proposal |
| --- | --- | --- |
| Duty direction | §7.9 and D27: 0 = off, 1000 = full on, because the AVR pin is high-time | S20 bench and `haus_ui.c`: the PC817 inverts that. `1000` is OFF, `0` is full heat, `100..500` is the measured Soll table. `haus_can_bus_send_pwm()` rejects `501..999` |
| Transmit gesture | F19 / D28: only a confirm button transmits | The detail page transmits on switch click and on every slider tick |
| Water out | §7.11 and Q9: no return probe, the zone is modelled | `haus_sensor_roles.cpp` seeds a third ROM as `SENSOR_FN_WATER_OUT`, clamped to the pipe. `haus_water_model.h` still says there is no return probe and that the table is the water-out temperature. The header is stale. The table is only the Soll hint. A missing live `0x180` identity keeps ΔT out of the law and blocks a table edit; it does not block AUTO |

N15 and D29 remain in force. They forbid a **safety** ramp, clamp, minimum pulse
and dead-man in the panel, because a panel-side interlock is bypassed by the
nRF remote and then looks like a guarantee. The minimum on/off times in this
paper are a comfort policy on AUTO's own commands. Manual OFF and the nRF path
stay outside them.

---

## 4. The one-time voltage campaign

### 4.1 Method and caveat

The owner's note is reproduced below. It says "Spannung via 100 Ohm". It does
not record whether this is the control voltage, a voltage across a 100 Ω series
shunt, or a divided measurement. The AWT interface is specified as max 24 V DC /
20 mA; 8 V across a 100 Ω shunt would be 80 mA, four times that limit, so the
campaign figure is unlikely to be a current shunt. It may be a different node, a
different day, or a meter on a divider.

The campaign was one-off and the ambient/load conditions are not recorded.
**Treat the volts as relative and the sequence as qualitative evidence of
state-dependent behaviour, not as a calibrated voltage table.**

### 4.2 Observations

| Control voltage / action | Observed boiler behaviour |
| --- | --- |
| 0–2.5 V | Heating off |
| > 2.5 V | Pump on |
| about 0.5 V around the pump threshold | Hysteresis |
| 3–7 V | 26 °C |
| 8 V | Burner on, strong |
| 7.5 V | 52 °C, burner weak |
| 6 V | Burner off |
| 8–20 V at 52 °C | Burner does not ignite |
| back to 8 V after a few minutes | Burner ignites |
| 16–20 V | Burner still weak |
| set 12 V and wait | Burner goes strong to about 70 °C, then alternates strong/weak |
| 10 V | Burner off; wait |
| falls to 48 °C | Burner still off |
| — | Burner weak on until 64 °C |
| falls to 50 °C | Burner on again |
| 9 V at about 62 °C | Burner off |
| falls to 48 °C | Burner weak on to about 58 °C |
| falls to 48 °C | Burner weak on |
| 11 V | Burner weak up to about 64 °C |
| falls to 50 °C | — |
| 12 V | Burner weak up to 70 °C |
| falls to 50 °C | — |
| 13 V | Burner weak up to 76 °C |
| 8 V | Falls to 50 °C |
| — | Burner weak on to about 56 °C |
| 6 V | Falls to ? |

### 4.3 What the campaign proves — and what it does not

Proves:

1. **There is an on/off threshold with hysteresis** for the pump, around
   2.5 V, with roughly 0.5 V hysteresis.
2. **There is thermal hysteresis and a minimum modulation.** The boiler
   restarts around 48–50 °C and modulates weak/strong; it does not simply hold
   a voltage-proportional temperature.
3. **The same voltage produces different states.** At about 52 °C, 8–20 V did
   not start the burner until the input was returned to 8 V and time passed.
   That supports "the boiler has memory". It does **not** prove that 8 V is the
   ignition voltage and that 16–20 V is less heat.
4. **The "return to 8 V and it ignites" row is confounded with time.** The
   campaign says the burner lit after a few minutes back at 8 V. The boiler's
   `Taktsperre` is about 3 minutes. Time and the voltage change cannot be
   separated from this note.
5. **The panel's usable range is not established.** The measured PC817 path
   reaches about 0.41–11.28 V at duty 1000..100, while the campaign discusses
   0–20 V. Whether the top of the boiler's demand range is reachable is a
   hypothesis to test, not a finding. Duty 0 is also missing from the bench
   voltage table: the AVR holds the pin solid low at permille 0 and solid high
   at permille 1000. The UI's "full heat" is duty 0, which may be a higher
   interface voltage than duty 100 or the same if the optocoupler is already
   saturated.

Consequence for control: a static table such as `duty 100 → 70 °C` is a
**quasi-steady observation**, not an actuator model. v1 adopts it as the
feed-forward hint. It is not a guarantee of immediate response, and it is not
treated as confirmed on the boiler's display.

### 4.4 Optional boiler visit

This visit is not a precondition for AUTO. Take it only when one of these is
true:

- the pipe model of §5.8 has a τ, a wait that is long against that τ has
  passed, and the flow-pipe reading still has no slope,
- the coil says the pump never started,
- duty 100 is not enough heat and someone wants a point above 70 °C.

Cover the duties that failed, not all eight by default. When the visit
happens:

1. Write down the voltmeter method, the node measured, and the load.
2. Set the boiler flow-temperature selector to the documented position.
3. At each chosen duty, record:
   - duty request and `0x280` report,
   - the boiler's own heating-temperature display (Soll),
   - the measured control voltage,
   - both pipe readings and room temperature,
   - pump/coil state.
4. Repeat after the boiler has settled, not immediately after the step.
5. Mark any DHW charge or boiler fault.

A display reading is the only observation that may change a knot, and a
person writes that knot by hand. A pipe reading never does. Duty 0 stays
manual-only until it has a point. Outside the adopted domain, AUTO holds.

Supervised thermal soaks measure the house and the pipe sensors. They are
part of the winter loop in §6.4, not a gate in front of the first release.

---

## 5. Control design

### 5.1 Authority, boot and foreign writers

```c
typedef enum {
  HEATER_MODE_OFF = 0,   /* manual OFF, latched */
  HEATER_MODE_MANUAL,    /* user-selected duty */
  HEATER_MODE_AUTO,      /* algorithm authority */
  HEATER_MODE_OVERRIDE   /* timed return to AUTO after a foreign writer */
} HeaterMode;
```

| Event | Result |
| --- | --- |
| Manual OFF while AUTO | `MODE_OFF`, send 1000 immediately, persist, cancel AUTO |
| Deliberate manual duty while AUTO | `MODE_MANUAL`, send the user value once, persist |
| AUTO control | Enter `MODE_AUTO` only when CAN is green and inputs are valid |
| Foreign `0x280` / nRF change | Yield: `MODE_OVERRIDE` for a bounded time (recommended 2 h), then return to AUTO unless a manual command intervened. Show the source badge |
| Reboot | Restore mode from `/heater.bin`; read `0x280`; send only when it disagrees with the restored mode |
| Sensor loss | Hold, then `AUTO HOLD`; do not silently switch modes |
| Unsynced clock | Hold; do not apply the day flow until `CanState.synced` |
| Schedule/sensor recovery | Cannot override `MODE_OFF` |

`OFF` is distinct from "manual duty = 1000". This prevents a schedule event or
reboot from resuming heat after the user switched it off.

### 5.2 Boot transmit rules

The AVR persists the last commanded PWM in its EEPROM log and restores it before
`pwm_out_begin()`. A panel reboot does not drop the output. The report on
`0x280` carries `PWM_SRC_EEPROM` until a live CAN or nRF command arrives.

The panel therefore sends only when `0x280` disagrees with the restored mode:

- **OFF** and the report is already `1000`: send nothing.
- **OFF** and the report is a heating duty: send `1000` once.
- **MANUAL**: send the saved duty only when the report differs.
- **AUTO**: send nothing until the first legal decision. Show
  `PWM_SRC_EEPROM` while the wire carries the restored duty.

A fresh `PWM_SRC_NRF`, or any `0x280` value this panel did not send, is a person.
AUTO yields. It does not overwrite the remote on its next 60 s tick.

Two limits of the EEPROM log:

- The heartbeat writes a changed duty at 60 s. An AVR power cut inside that
  minute restores the previous record. A panel reboot does not, because the AVR
  RAM still holds the new duty.
- When the newest record is a plain heartbeat rather than a boot marker or an
  explicit `1000`, the restore walks the recent window and keeps the **lowest**
  permille. On this installation a lower permille is more heat. A step toward
  less heat that has not yet been stamped as a boot marker can come back
  hotter. Once AUTO has a legal decision it may reassert; it does not reassert
  blindly at panel boot.

AUTO must not grow its own dead-man timeout. `eeprom_log.h` still says one drops
a stale restore; `main.cpp` removed it. A restored duty stands until the next
command.

### 5.3 The v1 law: scheduled flow

The first release has no room PI. It schedules a flow temperature and a real
off:

```
day:    T_flow = T_day              default 55 °C → duty 283 on the adopted table
night:  T_flow = T_day - 15 K       floored at 42 °C → duty 500
off:    duty = 1000                 manual OFF, a real schedule-off, or economy lockout
frost:  T_flow = a modest point on the adopted table, independent of the schedule
```

The forward map is `OffTableModel` in `src/haus_water_model.cpp`, provenance
`"off table v1"`. `target_from_duty()` already calls it. There is one table.
`estimate()` ignores water-in and chamber. It returns false below duty 100
and above duty 500, including duty 0 and duty 1000.

| Duty permille | Soll |
| --- | --- |
| 100 | 70 °C |
| 200 | 60 °C |
| 300 | 54 °C |
| 400 | 44 °C |
| 500 | 42 °C |

v1 adds the inverse on these same knots. Displayed Soll and commanded duty
share the table. Do not add a second copy in the controller, and do not feed
either result into `water_out`.

- Domain: Soll 42…70 °C ↔ duty 500…100. Outside that band, AUTO holds.
- The night floor sits on the endpoint 42 °C / duty 500. OFF is duty 1000 and
  **bypasses the inverse**. A false return from the model must not hold a
  heating duty when the law asked for OFF.
- Day 55 °C lies between 60 °C (duty 200) and 54 °C (duty 300). The fraction
  `(60 − 55) / (60 − 54) = 5/6`, so the duty is 283 when the 83.33 permille
  is rounded to the nearest permille. The test states that rounding.
- Duty 500 is the flat end of the table (400 → 44 °C, 500 → 42 °C). It is not
  a well-resolved setpoint. The steep part is 200…400 (60 °C down to 44 °C):
  a 50-permille step there is about 4 K, which matches the mover.
- Duty 0 stays manual-only. Adopting the table is not a reason to invent a
  point above 70 °C.
- 15 K is the AWT's recommended flow setback for the hotter system. It is the
  quantity the manual actually defines. `T_day` is tuned slowly, the way the
  AWT page says: change, wait hours, valves open.
- The proposal's old 20.5 / 17.0 °C room pair and the 45 °C night cap are
  removed in v1. They are replaced by two flow numbers.
- A real off is the only thing that creates a demand edge in v1. The boiler is
  already cycling internally; v1 does not cycle it from a 0.3 K room band.
- The schedule edge means "heating starts", not "room is warm". Add a fixed
  preheat lead (60–90 min) once the house time constant is known. An adaptive
  lead is a second project.
- A window-open freeze is not in v1. A fast room drop, especially with a
  humidity kick, false-triggers on a door. After the soaks it may suppress a
  preheat, behind a log line, and freeze a future integrator for about 15
  minutes.
- Until the clock is synced, hold the restored duty. Do not apply the day flow.
  The firmware TZ is `CET-1CEST,M3.5.0/2:00,M10.5.0/3:00`. The fall-back hour
  holds the current state until the next stable minute; the spring gap needs the
  same rule.
- Economy lockout (outdoor sensor, later) is the AWT's summer switch. It is the
  preferred energy feature before any room trim.

### 5.4 Frost protection and manual OFF

The AWT's "Heizung aus" programme still guarantees frost protection (AWT p4).
The boiler manual has no substitute: the appliance is only required to be
installed in a frost-free room, and the energy page tells the user to mind frost
protection themselves.

v1 policy:

- Manual OFF is latched and absolute.
- The OFF badge must say **"off — frost protection inactive"** (or include a
  separate frost-demand setting later). It must not silently imply protection.
- A separate frost demand uses room or pipe below a low threshold and a modest
  point on the adopted table, independent of the day/night schedule. 16 °C is
  a comfort floor, not freeze protection; choose a lower number. The pipe
  reading lags, so the threshold is not a water temperature.
- If a future `off_allows_frost` setting is added, it is an explicit user
  choice, not a hidden behaviour.

### 5.5 Future room trim (not v1)

After the SHT45 has been compared with a second thermometer (it sits on H1, on
a panel in the living room, next to a backlight that cannot be dimmed), add at
most a few kelvin of flow trim from the room error:

```
too cold  = T_room < T_set - 0.3 K
satisfied = T_room > T_set + 0.3 K

demand turns on  when too cold, after min_off, inside the start budget
demand turns off when satisfied, after min_on
while demand is on, duty = the adopted inverse of §5.3 (T_flow_set)
while demand is off, duty = 1000
```

Requirements:

- Radiator valves in the panel's room are fully open while trim is active
  (boiler p30).
- The trim is clamped, and the integrator is frozen whenever the transmitted
  duty does not change.
- One room sensor does not become the house thermostat.
- `T_flow_set` comes from the schedule, not from an integrator that runs during
  the off state.

### 5.6 Anti-short-cycling

Logical states:

```
IDLE ──demand──► HEATING ──no demand──► COOLDOWN ──min_off──► IDLE
```

Default parameters, all persisted and configurable:

| Parameter | Default |
| --- | --- |
| Control period | 60 s |
| Minimum on time | 10 min |
| Minimum off time | 15 min |
| Maximum demand edges per rolling hour | 2 |
| Minimum interval between duty changes | 5 min |
| Maximum duty step | 50 permille |
| Future room hysteresis (trim only) | ±0.3 K |
| Future flow deadband (trim only) | 2 K |

Rules:

1. OFF is only sent after `min_on`, except manual OFF and the pump guard.
2. ON is only sent after `min_off` and within the edge budget.
3. Duty may move while HEATING; only the on/off transitions are limited.
4. Frost protection may override `min_off` and the edge budget.
5. Manual OFF beats frost protection.
6. The budget counts **AUTO demand edges**, not burner ignitions. With
   `min_on + min_off = 25 min`, 2 per hour is the first budget that can
   actually reject a legal cycle. 4 was unreachable.
7. The guarantee is explicitly scoped to AUTO output. The boiler's own
   `Taktsperre`, minimum modulation, and any other CAN writer can still change
   the physical state.

### 5.7 Pump guard (later)

Input: `CanState.ac_rms_dc` — the 0x2C waveform RMS in **0.1 ADC count**, from
the current coil. It is an on/off activity indicator, not a calibrated ampere
value. The old amp scale does not match the resistor network.

```
request heat
and coil count < I_min for 10 min
and ΔT < 2 K
    -> send 1000, state PUMP_BLOCKED
```

Recovery:

- Stay at `1000` for 30 minutes, then allow one supervised demand.
- Clear early if the room has fallen further (a valve opened).
- Ignore the guard while the coil sample is stale.
- Record the installed pump-switch position first.

The guard must not use "current high" as the clear condition: sending `1000`
removes the heat demand, and with the pump switch in position 1 the pump runs
only on heat demand. The current stays low and the latch would never clear. With
the switch in the ON position the pump runs continuously during heating, so the
"low current means all valves closed" reading is a different test. A 40 s run-on
also leaves a current tail after a real demand ends. The 3-minute grace equals
the `Taktsperre`, so a demand that has not yet started the burner must not look
like a blocked circuit.

The home screen's separate "pump on/off" text is derived from duty
(`pwm_permille > 0 && pwm_permille < 1000`), so duty 0, which the UI calls full
heat, displays as pump off. Experiment `pump_state` must come from the coil, not
from that label.

The `ΔT < 2 K` term is a difference of two pipe-wall readings. It opens on τ
from §5.8, which has not been measured. A 10-minute test can therefore
false-trip on a slow pipe. The guard stays later for this reason as well as
for the missing coil threshold.

The guard is equipment protection, not a safety interlock. It suspends AUTO
only; manual OFF and manual duty remain untouched.

### 5.8 Pipe readings, not water

On this installation the cooler pipe is the return and the hotter pipe is the
flow. Both DS18B20s are clamped to the outside of those pipes. The names
`water_in` and `water_out` in the log are pipe-wall temperatures. The panel
never sees the bulk water. Soll is the adopted table: the temperature the
boiler is asked to make the water. It is not either probe.

The boiler's own flow NTC, time constant about 100 s (§3.2), sits in the
water. It is a different sensor. The pipe, the contact and the DS18B20 body
are slower than that, and a wall reading can settle short of the water
temperature. A pipe that finishes below Soll is then the sensor, not a missed
setpoint and not a reason to raise the duty.

The relation to identify in the winter loop, and not to close the v1 loop on,
is:

```
τ · dT_pipe_out / dt = T_inf − T_pipe_out
T_inf = T_pipe_in + α · (T_soll − T_pipe_in)
```

τ is identified. It is not the boiler's 100 s. α lies between 0 and 1 and is
identified; α = 1 is not assumed. When α is below 1, the steady pipe reading
stays below Soll even if the boiler is holding the water there.

The return probe is the same kind of sensor, so `water_out − water_in` is a
difference of two wall readings. The sign runs from the cooler pipe to the
hotter pipe. It is not the heat delivered to the house, and it is not a
substitute for α, until this model exists. v1 keeps that difference out of
the law.

`chamber_target_c` is `target_from_duty()` of the duty the panel already
commanded. Feeding it back, or feeding `estimate()` into `water_out`, would
confirm the command that produced it. v1 commands Soll through the inverse
and does not read `T_pipe_out` as an error.

Whether the flow probe is really on the bus is unresolved across documents.
The roles seed says it is installed; the parent whitepaper says it is not.
AUTO still runs if the live `0x180` identity shows no water-out probe:
scheduled flow does not need the pipes. The refinement note then says "no
pipe, table unchanged". A missing probe blocks a table edit. It does not
block heat. The fault text says which probe is missing.

### 5.9 Failure handling

| Condition | Behaviour |
| --- | --- |
| SHT45 stale | In v1 the schedule does not need it; if trim is active, hold the last duty. Badge `AUTO HOLD` |
| Water sensors stale | The pipe model leaves the winter fit; scheduled flow continues on the adopted table. No undefined "reduced authority" |
| Clock unsynced | Hold the restored duty; do not apply day flow |
| CAN not green | AUTO unavailable, manual controls disabled as today |
| `0x280` conflicts with request | Log once, show actual, do not retry-loop (S20.09) |
| Command outside legal range | Reject, log, hold |
| Foreign writer / nRF | `MODE_OVERRIDE` for a bounded time, then return to AUTO |
| Boiler fault or DHW charge | Log as a disturbance; do not fit it as plant gain |

There is no second, undefined open-loop curve as a fallback. The fallback is
HOLD plus a reason.

---

## 6. Experiment and Model Lab

### 6.1 Why a tool is required

The boiler has internal state, hysteresis, a 3-minute start delay and its own
100 s flow sensor. The panel's DS18B20s are slower still, and they read the
pipe. v1 does not wait for a fitted plant model. The lab answers, from
operation logs and only later from a supervised visit:

- How slowly does the flow-pipe reading follow a held Soll, and what τ fits?
- What α does it settle at, and does that stay below 1?
- How does α move with the return pipe, the room and the number of open valves?
- Which intervals are not plant data, because `duty_req` and `duty_rpt` disagree
  or a DHW charge passed?
- When do the thermostats block flow, and what does the coil look like?
- Only on the §4.4 branch: does the boiler display match a knot, and does the
  PC817 range cover the boiler's useful range?

### 6.2 Architecture

```
┌────────────────────┐     CSV/HTTP/SD      ┌──────────────────────┐
│ panel experiment   │──────────────────────►│  trace store         │
│ logger (1 Hz)      │                      │  experiments/<id>/   │
└────────────────────┘                      └──────────┬───────────┘
                                                       │
                       ┌───────────────────────────────┼──────────────────────┐
                       ▼                               ▼                      ▼
              deterministic analysis          LLM analyst            report / model.json
              (numpy/scipy)                   (hypotheses,            plots, Markdown
              pipe model τ, α,             experiment plans,
              hysteresis, regression)          explanations)
```

Proposed location: `tools/heater_model/`.

| Component | Purpose |
| --- | --- |
| `collect.py` | Read operation logs over HTTP/SD, or an external logger CSV |
| `experiment.py` | Optional supervised steps; refuses to run unless `--allow-write` is given explicitly. Not the first action |
| `identify.py` | The pipe model of §5.8, then regime detection. It does not rewrite the duty knots |
| `llm_analyst.py` | Sends downsampled traces and manual extracts to an LLM with tool access; produces hypotheses and experiment proposals, never actuator commands |
| `report.py` | Generates `report.md`, plots, and a machine-readable `model.json` |
| `model.md` | Model card: inputs, states, parameters, confidence, validity range |

`experiment.py` and `llm_analyst.py` are separate programs so a tool list cannot
grow `--allow-write` into the analyst.

### 6.3 Trace format

At least 1 Hz during experiments, with monotonic timestamps:

```csv
epoch_ms,seq,duty_req,duty_rpt,water_in_c,water_out_c,room_c,room_rh,
ac_rms_dc,ac_angle_cdeg,pump_state,can_ok,target_c,note
```

`duty_req` is what the panel sent; `duty_rpt` is what `0x280` reported. Keeping
both is mandatory: a TX failure or another writer must not be mistaken for
plant dynamics. `water_in_c` and `water_out_c` are pipe-wall temperatures.
`target_c` is the Soll hint, never a pipe value and never bulk water.
`pump_state` comes from the coil, not the duty-derived home label.

Experiment files stay in their own directory. They do not go into the minute
record.

### 6.4 Order of operations

**Phase 0 — adopt.** The v1 inverse is off-table v1, labelled adopted. No
boiler visit. Domain, day duty 283, night floor duty 500, and the OFF bypass
are those in §5.3.

**Phase 1 — control.** Host tests of §7.1. Then a short shadow: one schedule
edge, or a few hours, only to see that the logged decision stays inside
`100…500` or `1000`. Several days of shadow before the first transmit are not
the plan. Then AUTO transmits. Manual OFF still latches and still wins.

**Phase 2 — first operation window.** A few days. Three is enough. Log
`duty_req`, `duty_rpt`, both pipes, room, coil, mode and reason. Do not fit
a model during the window. The window succeeds when the house ran and the log
exists. Matching the flow pipe to 55 °C is not the pass criterion. A missing
water-out probe does not fail the window (§5.8).

**Phase 3 — winter iteration.** Repeat through the heating season. A person
accepts each result. Stop when τ and α are stable across one cold week and
one mild week, or when comfort is simply good enough.

Each pass:

1. Split the log into day, night and off. Drop every interval where
   `duty_req` and `duty_rpt` disagree.
2. On intervals that held one duty long enough to settle, record median
   pipe-out, pipe-in and coil RMS.
3. Fit §5.8. A settled gap under Soll is that model. It is not a wrong knot.

Then one of these outcomes, and only one:

| Outcome | When | What changes |
| --- | --- | --- |
| Unchanged | The same τ and α still describe the pipes | Nothing in the duty table |
| Pipe-model note | τ or α moved, or the fit is still too thin to trust | Table stays. Record the new τ and α beside it |
| Load term | α follows pipe-in, room, or how many valves are open | A term beside the table. The five knots stay |
| New knot | A boiler-display reading exists for that duty | That one knot, written by hand. A pipe reading never justifies it |
| Boiler visit | §4.4's branch | The duties that failed, not all eight by default |

Until τ has been seen once, a few quiet days are not themselves a reason to
visit the boiler.

A DHW charge or a boiler fault is a disturbance. Mark it when the log shows a
sudden pipe jump at constant duty. That does not require standing at the
appliance.

The supervised steps below stay available and stay optional. They are not
step 1. The tool never exceeds the panel's legal values, never runs
unattended, and stops on a stale pipe reading or an unexpected pump state.

- Off baseline and step holds, only at duties on the adopted table, long
  enough for the pipe rather than for the boiler's 100 s sensor.
- On/off cycles, to compare the panel's minimum times with the boiler's
  3-minute delay. The pipe slope is a late witness of that delay.
- Load variation at different rooms and a known number of open thermostats.
- Pump-block test: close all thermostats with supervision, record the coil
  and both pipes, then reopen. Explicitly approved, and only after τ and α
  exist, so a slow pipe is not read as a blocked circuit.

### 6.5 Identification

Two relations, kept apart. Fitting one gain from duty to a pipe reading
reports a number that is neither Soll nor the water temperature.

- **Adopted table.** The five knots of §5.3 are the v1 inverse. They are not
  a fit, and a pipe residual does not move them.
- **Pipe model.** §5.8, fitted on intervals that held one duty. The product
  is τ and α, plus a note when the fit is thin. α = 1 is a result, not a
  constraint.
- **Load term.** If α follows pipe-in, room or the number of open valves, add
  that term beside the table. The knots stay.
- **Later, and only inside one regime:** a first-order description of burner
  behaviour. Do not fit one curve across the pump threshold and the ignition
  threshold. Do not use the boiler's 100 s as τ for the pipe.
- **Regime detection:** segment into OFF, pump-only, ignition wait, weak fire,
  strong fire, modulation and lockout. Start with thresholds on coil RMS and
  the slope of the flow-pipe reading; only then consider a hidden Markov
  model. The slope is the pipe's slope.
- **Hysteresis and start delay:** the boiler's own behaviour. A positive
  pipe slope is a late and attenuated witness. Confound the delay with
  `Taktsperre` and with τ before calling it an ignition time.

### 6.6 The LLM analyst

The LLM is useful for:

- summarizing long experiment logs,
- proposing which regime boundaries to inspect,
- generating candidate explanations from the manual and the traces,
- drafting experiment plans and reports,
- answering "what changed between run A and run B?".

The LLM must not:

- produce the final numerical model parameters,
- send actuator commands,
- decide safety limits,
- replace cross-validation.

Guardrails:

- numerical fits come from deterministic code with fixed seeds;
- every LLM claim must cite a computed statistic or a manual passage;
- hypotheses are tested on held-out data before being accepted;
- prompts, model name and data snapshot are versioned in the experiment
  directory;
- the tool refuses write access unless explicitly enabled and a human is
  present;
- the lab's first numerical product is the pipe-model note, produced by
  deterministic code. It may say the duty table was left alone. The LLM is
  optional and off the device. No iteration writes firmware by itself.

### 6.7 Outputs

- `model.json`: τ, α, any load term, uncertainty and the validity range.
  The five duty knots are recorded as adopted inputs, not as fitted outputs.
- `report.md`: what was tested, what was learned, what remains unknown. The
  first operation window's note lives here and may say "table unchanged".
- Plots: pipe readings against Soll, the approach toward `T_inf`, residuals,
  regime timeline, coil against the pipe difference.
- A short firmware-facing summary, reviewed by a person before any number is
  copied in. A knot changes only from a boiler-display reading. The state
  machine and the safety rules do not depend on the learned model.

---

## 7. Test environment and use cases

### 7.1 Level 1: pure controller tests

`test/test_heater_control/`, same Makefile pattern as `test_water_model`:

- `-std=c++17 -Wall -Wextra -Wconversion -Wshadow -Werror`
- `make test`
- `make sanitize` with address/undefined sanitizers

The demand predicate and flow setpoints must be written in the header before
these tests are written. Otherwise the tests will be padded until they pass.

Required cases (v1 law):

| # | Use case | Assertion |
| --- | --- | --- |
| 1 | Night onset | Flow floors at 42 °C, duty 500; no off edge |
| 2 | Day transition | Flow returns to the day value, subject to the mover |
| 3 | Real off / economy | OFF after `min_on`; at most 2 demand edges per rolling hour |
| 4 | Manual OFF during AUTO | 1000, mode OFF, no AUTO sends for 24 simulated hours |
| 5 | Deliberate manual takeover | Mode MANUAL, one user frame, AUTO cancelled |
| 6 | AUTO re-entry | No send until preconditions valid |
| 7 | Missing SHT45 in v1 | Scheduled flow continues; trim, if active, holds |
| 8 | Unsynced clock | Hold the restored duty; no day-flow jump |
| 9 | Pump blocked | OFF latch after debounce; recovery by 30 min timer or room drop, not by current |
| 10 | Coil stale | Guard inactive, AUTO continues |
| 11 | Frost demand | Modest flow despite night; manual OFF still wins and OFF badge says frost is off |
| 12 | DST/weekend schedule | Correct local-time evaluation, fall-back and spring-gap hold |
| 13 | Adopted inverse | Round trip on the five knots; day 55 °C → duty 283; night floor → duty 500; OFF is 1000 without consulting the model; AUTO never emits duty 0 |
| 14 | Command domain | Always 1000 or 100..500 |
| 15 | Foreign writer | `MODE_OVERRIDE`, then bounded return to AUTO |
| 16 | Boot with restored duty | No transmit when `0x280` matches the mode; `PWM_SRC_EEPROM` visible |
| 17 | Seven-day simulation | Bounded demand edges, no invariant violation |

### 7.2 Level 2: closed-loop simulator

Use a simple plant model in `host_harness/src/heater_sim.cpp`:

- the pipe-wall lag of §5.8, with α allowed below 1,
- boiler hysteresis and minimum modulation, seen by the pipe only after τ,
- a 3-minute ignition delay,
- optional load disturbance on α.

Keep it for state-machine invariants. Stability on the real boiler comes from
the short shadow and the adopted table, not from this simulator. A simulator
the winter loop has not identified will bless a controller that matches the
simulator. v1 does not close on the simulated pipe.

### 7.3 Level 3: replay

A current minute record has room, both pipes, coil RMS, coil angle, the
reported duty and the meter registers. It has no AUTO mode and no reason.
`target_c100` is 0 outside `100..500`, so that field alone cannot tell OFF
from full heat; `duty_permille` can. Files from before the 44-byte record
have neither. Replay can drive the plant side. It cannot score an AUTO
decision. The transition log from the definition of done is the record that
can.

Replay `dump_2026-10-23/sd/data_*.bin` and `rollup.bin` through the existing
dump loader for the plant side, with explicit mode/duty injection.

### 7.4 Level 4: UI render tests

Extend the host renderer with:

- `output_heater_auto.png`
- `output_heater_night.png`
- `output_heater_hold.png`
- `output_heater_pump_blocked.png`
- `output_heater_manual.png`
- `output_heater_frost_off.png`

### 7.5 Level 5: shadow mode on the bench

Before AUTO is allowed to transmit, compute and log the decision while the
manual path is authoritative. This shadow is the short one in §6.4: one
schedule edge, or a few hours. It is not a multi-day gate.

Agreement between planned and actual duty is the wrong pass/fail: they differ
because a person is holding the slider. Log instead:

- the decision and the reason,
- the suppressed reason (`min_on`, unsynced, NRF yield, `AUTO HOLD`),
- the flow and room traces,
- whether the planned duty stayed inside `100..500` or `1000`,
- the switching rate.

Enable transmit after that short review. The following days are the operation
window, and during them AUTO is already transmitting.

---

## 8. UI integration

On the HEATER detail page, keep the existing switch and slider, and add a mode
control:

```
AUTO     [ OFF | MANUAL | AUTO ]
         requested 320 / 1000
         in force  300 / 1000
         AUTO heating, flow 55 -> 42 °C
```

- The existing switch always means manual OFF/ON.
- Takeover is a deliberate control: the mode segment, or a slider **release**
  that sends one frame. While AUTO is selected, the slider displays the
  commanded duty and does not write the bus.
- A drag that started on the AUTO screen does not change mode at
  `VALUE_CHANGED`. The current page sends on every `VALUE_CHANGED`; that must
  not become the AUTO takeover gesture and must not produce a burst of durable
  `0x240` frames.
- AUTO is disabled when CAN is not green or inputs are invalid, with a reason.
- Badges: `AUTO`, `AUTO HOLD`, `PUMP BLOCKED`, `OVERRIDE`, `NIGHT`,
  `FROST`, and `off — frost protection inactive`.
- `PWM_SRC_EEPROM` is shown while the wire carries the AVR's restored duty.
- Persist mode and schedule in a versioned `/heater.bin` with **magic, version,
  CRC32 over the payload**, following the day-file/`rollup.bin` pattern — not
  `sensor_roles.bin`, which has no CRC. Reject a bad file, keep the previous
  mode in RAM, and never come up as AUTO on a bad file.
- Request a full redraw after writes, as `haus_config_set_can_enabled()` does.
- Do not add AUTO mode to the minute record in v1. Log transitions first; add
  a chart series only after the algorithm is trusted.

---

## 9. Integration order

1. **Inverse of off-table v1 and the host tests.** No boiler visit.
2. **Config persistence and UI mode.** Manual transmission only.
3. **Short shadow.** AUTO computes, logs, does not transmit.
4. **Enable AUTO** with the adopted defaults and deliberate takeover tested.
5. **Operation window** of a few days. The note may say the table was left
   alone.
6. **Pump guard** after the coil threshold, the pump-switch position, and a
   first τ and α. It does not precede the window.
7. **HTTP observability:** `/heater` JSON with mode, state, setpoints, requested
   duty, reason, remaining minimum times and edge count.
8. **Winter iteration** updates the pipe-model note. A knot changes only when
   a person copies in a boiler-display reading. The state machine and the
   safety rules do not depend on the learned model.
9. **Room trim** and **outdoor economy** are later modes with their own
   parameters and tests.

---

## 10. Risks

| Risk | Mitigation |
| --- | --- |
| Treating the boiler as linear | Scheduled flow, slow moves, adopted table, short shadow, no open-loop guarantee |
| Unconfirmed duty table | Legal domain only, slow steps, manual OFF, winter correction. The boiler visit is the disambiguator, not the entry ticket |
| Pipe reading chased toward Soll | v1 does not close on the pipe. A settled gap is α, not an error, and it does not move a knot |
| Pipe difference used as heat | Kept out of the law until τ and α exist |
| Short cycling | Minimum on/off, 2-edge budget, boiler `Taktsperre` acknowledged; count demand edges, not ignitions |
| AUTO overrides a human | Manual OFF latches; takeover is deliberate; foreign writer gets a timed override |
| Sensor loss causes hunting | HOLD and explicit badge; no undefined backup curve |
| Pump guard false positive | Stays later until τ and α exist. Then 30 min timer/room-trend recovery, stale-input rejection, pump-switch precondition |
| LLM hallucination in analysis | LLM is advisory; numerical fits are deterministic and validated |
| No outdoor sensor | Scheduled flow and frost; document the lost weather compensation |
| Boiler model differs from manual | Treat the manual as family-level; identify the installed unit |
| Parent whitepaper contradicts bench | Use S20/firmware for duty direction and transmit gesture; update the parent text later |
| AWT flow setback confused with room setback | Name the quantity: °C flow, not room setpoint |

---

## 11. Decisions after review

| # | Decision | Recommendation |
| --- | --- | --- |
| 1 | What does v1 command? | Two scheduled flow setpoints and a real off. Room PI waits. |
| 2 | Day flow, night setback | Day 55 °C → duty 283 on the adopted table. Night floors at 42 °C → duty 500. Replace 20.5 / 17.0 until room trim exists. |
| 3 | Minimum on/off and edge budget | Keep 10 / 15 min. Set the budget to 2 per rolling hour so it can fire. Count demand edges, and say so. |
| 4 | Water probes | Both are pipe-wall sensors. Read the live `0x180` identities. A missing probe does not block AUTO. The pipe difference stays out of the law until τ and α exist. |
| 5 | SHT45 missing | v1 scheduled flow does not need it. If trim is active, hold. No undefined backup curve. |
| 6 | Pump guard | Later, and only after a first τ and α. Threshold in ADC counts. Recovery by timer and room trend. Record the pump-switch position first. |
| 7 | Does OFF survive reboot? | Yes. The AVR restores the last duty from EEPROM, and `1000` restores as off. The panel sends `1000` only when `0x280` disagrees. AUTO transmits nothing and displays `PWM_SRC_EEPROM`. |
| 8 | Unsynced clock | Hold. Do not apply the day setpoint. |
| 9 | nRF or any foreign `0x280` | Yield. Timed return to AUTO (`MODE_OVERRIDE`) rather than sticky MANUAL. Manual OFF stays sticky. |
| 10 | Frost while OFF | Manual OFF is absolute in v1; badge "frost protection inactive". A later `off_allows_frost` setting may restore AWT-style frost. |
| 11 | Boiler visit | Not before the first AUTO. Off-table v1 is the inverse. Visit only on the branch in §4.4, and only a display reading may change a knot. |
| 12 | Outdoor sensor | Look for the old AF before buying one. Economy lockout is the feature it unlocks. |
| 13 | Schedule clock | A battery RTC removes the WiFi coupling. Until then, hold across an unsynced boot. |

---

## 12. Review disposition

The 1.1 rows record the review that revision 1.1 absorbed. Where 1.2 disagrees,
the later table wins.

| Review item | Resolution in revision 1.1 |
| --- | --- |
| 1.1 PI and on/off machine not one controller | v1 is scheduled flow; the demand predicate is defined for the later trim mode (§5.3, §5.5) |
| 1.2 Burner starts not observable | Count AUTO demand edges, not burner ignitions; boiler `Taktsperre` and minimum modulation acknowledged (§5.6) |
| 1.3 Start budget cannot bind | Budget reduced to 2 per rolling hour (§5.6) |
| 1.4 Night cap and frost cancel | Room pair and 45 °C cap removed; flow setback plus separate low frost demand (§5.3, §5.4) |
| 1.5 Pump latch cannot clear | Recovery by 30 min timer or room drop; threshold in ADC counts; pump-switch precondition (§5.7) |
| 1.6 ΔT sign and modelled temperature | `water_out − water_in`; both pipes fresh measurements; never `chamber_target_c` (§5.8) |
| 1.7 Campaign volts used as absolute | §4.1 caveat retained; "reachable range" demoted to hypothesis; duty 0 added to required measurement (§4.3, §4.4) |
| 1.8 Boot transmit / EEPROM / NRF / unsynced | Boot rules, foreign-writer override, unsynced hold and TZ specified (§5.1, §5.2) |
| 1.9 Slider tick cancels AUTO | Deliberate takeover only; AUTO slider does not write the bus (§8) |
| 1.10 Failure table contradicts open decision | One HOLD policy; undefined curve removed (§5.9) |
| 1.11 Integrator gains/windup | v1 has no integrator; trim mode later with frozen-integral rule (§5.5) |
| 1.12 `sensor_roles.bin` CRC pattern | `/heater.bin` uses magic, version, CRC32 like day files; bad file never AUTO (§8) |
| §2 Manual facts | AWT open loop, flow setback, economy stop, room-control installation rule, storage/DHW disturbance, continuous-controller preconditions added (§3.2, §3.3, §6.4) |
| §3 Parent whitepaper stale | Added the duty-direction, transmit-gesture and water-out table (§3.5) |
| §4 v1 law | Adopted: scheduled flow, no room PI, real off, later bounded trim (§5.3–§5.5) |
| §5 Other ideas | Timed override, window-open freeze deferred (§5.3), shadow metric, simulator/replay limits, LLM fences, regime-wise identification added (§7.5, §6.5, §6.6) |
| §7 Decisions | Replaced with the 13-decision table (§11) |
| §8 Definition of done | Amended below (§13); amended again in 1.2 |

### Folded in revision 1.2

| Point | Where it now lives |
| --- | --- |
| No boiler visit before AUTO | §1, §4.4, §9, decision 11 |
| Off-table v1 is the adopted inverse; day 283, night 500, OFF bypasses it | §5.3, test 13 |
| A few days of operation, then a winter loop | §6.4 |
| Both DS18B20s read the pipe wall. Water-out is slow and may settle below Soll | §5.8 |
| That gap is τ and α. It does not edit a knot | §6.4, §6.5 |
| The pipe difference is not heat delivered, and it stays out of the v1 law | §5.8 |
| A false model return must not refuse OFF. Duty 0 is not invented | §5.3 |
| `haus_water_model.h` still says there is no return probe and that the table is water-out. The header is stale; correct it when that file is next edited | §3.5 |

---

## 13. Definition of done for the first AUTO release

- The inverse tests pass: five knots, day 55 °C → duty 283, night floor →
  duty 500, OFF is 1000 without the model, AUTO never emits duty 0.
- Controller and config compile on device and host.
- All 17 use cases pass, including sanitizer runs.
- Manual OFF and deliberate manual takeover are demonstrated on the bench.
- The OFF badge states whether frost protection is active.
- The short shadow has been reviewed, and AUTO has then transmitted.
- A note exists after the first operation window. It may say the table was
  left alone and that τ and α are not fitted yet.
- Every AUTO transition is logged with mode, reason, flow setpoint, duty and
  timer state.
- The UI shows mode, source and reason without ambiguity.
- A boiler visit and a fitted pipe model do not gate this release.

---

## 14. Source extracts used

### 14.1 AWT 1

- `MAX. 24 VDC/20 mA` controller interface (p2).
- Temperature choice ±20 °C in 5 °C steps, slope, economy automation (p3).
- Programmes, setback 0–40 K in 5 K steps, daily timer, frost protection in
  "Heizung aus" (p4).
- Recommended settings, including setback 10–15 K, slope 0.7–1.2, economy
  automation 15–20 °C (p4).

### 14.2 GU-E/GU-1E/GG-E/GG-1E

- Flow sensor NTC, time constant about 100 s; flow up to about 90 °C; minimum
  modulating power (p5).
- External continuous controller on RT/WT/EWT; remove the 24 V bridge; cancel
  night setback below −12 °C outdoor (p21).
- Pump selector: continuous or demand-only; optional 40 s run-on (p21).
- Burner start delay about 3 minutes (`Taktsperre`) (p21).
- Room-controller installation rule: radiator valves in that room fully open,
  no furniture or curtains (p30).
- Energy-saving advice: night setback, 1 K room reduction up to 5 % energy
  (p30).

### 14.3 Project sources

- `docs/whitepaper.md` §7.9 actuator control, N15 safety boundary.
- `docs/todos.md` S20 bench notes: PC817 inversion, `0.41 V` at 1000,
  `11.28 V` at 100.
- `src/haus_gui.cpp` `target_from_duty()` measured table.
- `include/haus_water_model.h` and `src/haus_water_model.cpp`.
- `src/haus_sensor_roles.cpp`: the two probes are on the water-in pipe and the water-out pipe.
- `AVR/01_heizung_temp/src/pwm_out.cpp`, `eeprom_log.cpp`, `main.cpp`.
- `test/test_*` host-test pattern, including `test_water_model`.
- `host_harness/` renderer and `dump_2026-10-23/` data.
- Review of 2026-10-23, incorporated in revision 1.1. The file was removed.
- Measurement-gate review of 2026-10-23, incorporated in revision 1.2. The file was removed.
