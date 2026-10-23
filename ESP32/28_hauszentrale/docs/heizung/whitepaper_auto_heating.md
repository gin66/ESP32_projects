# Automatic Heating Control for 28_hauszentrale

**Status:** design proposal, not implemented  
**Revision:** 1.1, 2026-10-23 — incorporates the review of that date. The
review file was removed once this revision absorbed it.  
**Scope:** `ESP32/28_hauszentrale` and the Wolf gas boiler installation served by
`AVR/01_heizung_temp`  
**Related:** [`../whitepaper.md`](../whitepaper.md) §7.9, §7.11; N15; S20  
**Sources:** [`guegu1e.pdf`](guegu1e.pdf) (Wolf GU-E/GU-1E/GG-E/GG-1E planning and
installation manual), [`3044603_0599_AWT1_analoger_witterungsgefuehrter_Temperaturregler.pdf`](3044603_0599_AWT1_analoger_witterungsgefuehrter_Temperaturregler.pdf)
(Wolf AWT 1 analog weather-compensated controller), the 2026-10-18 S20 bench
notes, the owner's one-time voltage campaign, and the review dated 2026-10-23.

**Revision history**

| Rev | Date | Change |
| --- | --- | --- |
| 1.0 | 2026-10-23 | First design proposal |
| 1.1 | 2026-10-23 | Processed review: one demand law, scheduled flow instead of room PI, corrected start-budget and night/frost logic, pump-latch recovery, ΔT sign, boot/NRF/unsynced behaviour, deliberate takeover, CRC persistence, model-lab order, test cases and decisions. The review file was removed after this text was checked against it |

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
2. The existing `target_from_duty()` table remains a steady-state **interface
   map**, not a plant model. It must be confirmed with the boiler's own
   temperature display before it is used.
3. A dedicated **Experiment and Model Lab** identifies the real relation between
   PWM duty, control voltage, water in/out temperatures, room temperature and
   pump current. The lab uses deterministic system identification; an LLM is an
   analyst and experiment planner, not part of the control loop.
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
- A data-analysis tool for the PWM/voltage/water relation.

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
| **Soll** | Boiler flow-temperature target, in °C. The panel derives a *hint* from duty through the measured interface map. |
| **AUTO** | The new control mode. |
| **MANUAL** | User-selected duty. |
| **OFF** | Latched user intent: no automatic heating. |
| **Demand edge** | A transition of AUTO's logical demand from off to on, or on to off. It is not a burner ignition. |
| **Interface map** | The confirmed quasi-steady relation between duty and the boiler's displayed flow setpoint. |

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
record the control voltage with an external logger during experiments. Without
one of these, the model lab can fit only the black-box relation
`duty → water temperature`, not `duty → voltage → boiler state`.

### 3.5 Parent whitepaper is stale in three places

Implementing AUTO by "following `docs/whitepaper.md`" will undo the bench.

| Topic | Parent whitepaper | Firmware and this proposal |
| --- | --- | --- |
| Duty direction | §7.9 and D27: 0 = off, 1000 = full on, because the AVR pin is high-time | S20 bench and `haus_ui.c`: the PC817 inverts that. `1000` is OFF, `0` is full heat, `100..500` is the measured Soll table. `haus_can_bus_send_pwm()` rejects `501..999` |
| Transmit gesture | F19 / D28: only a confirm button transmits | The detail page transmits on switch click and on every slider tick |
| Water out | §7.11 and Q9: no return probe, the zone is modelled | `haus_sensor_roles.cpp` seeds a third ROM as `SENSOR_FN_WATER_OUT`. `haus_water_model.h` still says there is no return probe. Until a live `0x180` identity confirms it, ΔT stays out of the law |

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
**quasi-steady observation**, not an actuator model. It is acceptable as an
initial feed-forward hint. It is not acceptable as a guarantee of immediate
response.

### 4.4 The first measurement required

One supervised visit, before any thermal soak:

1. Write down the voltmeter method, the node measured, and the load.
2. Set the boiler flow-temperature selector to the documented position.
3. At duty 0, 100, 200, 300, 400, 500 and 1000, record:
   - duty request and `0x280` report,
   - the boiler's own heating-temperature display (Soll),
   - the measured control voltage,
   - water in/out and room temperature,
   - pump/coil state.
4. Repeat after the boiler has settled, not immediately after the step.
5. Mark any DHW charge or boiler fault.

This produces the **interface card**: eight points that turn
`target_from_duty()` from a bench guess into a confirmed table. Outside the
confirmed points, AUTO holds. Duty 0 stays manual-only until it has a point on
the map.

The nine 30-minute thermal soaks are a later experiment. They measure the house,
not the interface.

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
day:    T_flow = T_day              default 55 °C, inside the confirmed map
night:  T_flow = T_day - 15 K       floored at the bottom of the map (about 42 °C)
off:    duty = 1000                 manual OFF, a real schedule-off, or economy lockout
frost:  T_flow = a modest confirmed point, independent of the schedule
```

- `T_flow` is converted to duty through the confirmed interface map. Outside the
  confirmed points, AUTO holds.
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
- A separate frost demand uses room or water below a low threshold and a modest
  confirmed flow, independent of the day/night schedule. 16 °C is a comfort
  floor, not freeze protection; choose a lower number.
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
while demand is on, duty = inverse_interface(T_flow_set)
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

The guard is equipment protection, not a safety interlock. It suspends AUTO
only; manual OFF and manual duty remain untouched.

### 5.8 Water temperatures and ΔT

On this installation the cooler pipe is water in (return) and the hotter pipe is
water out (flow). Heat delivered to the house is proportional to:

```
ΔT = water_out - water_in
```

not `water_in - water_out`. The earlier draft had the sign wrong.

`chamber_target_c` must not be one side of that difference. It is
`target_from_duty()` of the duty the panel already commanded; feeding it back
confirms the command that produced it. The controller must use both
`temp_by_fn[WATER_IN]` and `temp_by_fn[WATER_OUT]`, both fresh measurements,
neither equal to the Soll hint.

Whether the flow probe is really on the bus is unresolved across documents. The
roles seed says it is installed; the parent whitepaper says it is not. Until a
live `0x180` identity says the water-out ROM is present, ΔT stays out of the
control law and the fault text says which probe is missing.

### 5.9 Failure handling

| Condition | Behaviour |
| --- | --- |
| SHT45 stale | In v1 the schedule does not need it; if trim is active, hold the last duty. Badge `AUTO HOLD` |
| Water sensors stale | ΔT leaves the law; the scheduled flow can continue if the interface map is valid. No undefined "reduced authority" |
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

The boiler has internal state, hysteresis, a 3-minute start delay and a 100 s
sensor time constant. The one-time campaign is not enough to design a stable
controller. We need a repeatable way to answer:

- What does each duty actually do in steady state?
- How long does it take?
- What are the on/off and ignition thresholds?
- How does the response change with water inlet temperature, room temperature
  and load?
- Does the panel's PC817 range cover the boiler's useful range?
- When do the thermostats block flow, and what does pump current look like?

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
              FOPDT, state detection,          experiment plans,
              hysteresis, regression)          explanations)
```

Proposed location: `tools/heater_model/`.

| Component | Purpose |
| --- | --- |
| `collect.py` | Read the panel's high-rate trace over HTTP/SD, or an external logger CSV |
| `experiment.py` | Safe, supervised step/PRBS sequences; refuses to run unless `--allow-write` is given explicitly |
| `identify.py` | Regime detection, static gain, FOPDT fit, hysteresis and delay estimation, cross-validation |
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
plant dynamics. `target_c` is the Soll hint, never a measured pipe value.
`pump_state` comes from the coil, not the duty-derived home label.

Experiment files stay in their own directory. They do not go into the minute
record.

### 6.4 Experiment order

1. **Interface card (first):** the eight-point measurement in §4.4. This is the
   v1 model-lab gate. It replaces the nine 30-minute soaks as the first
   experiment.
2. **Precondition:** steady state, known flow-temperature selector, pump mode,
   thermostat positions, DHW state documented.
3. **Off baseline:** 30 min at duty 1000.
4. **Step series:** use only points on the confirmed interface card, held long
   enough for a steady state. Randomize the order between sessions.
5. **On/off cycles:** verify the minimum on/off behaviour and the boiler's own
   3-minute delay.
6. **Load variation:** repeat at different room temperatures and with a known
   number of open thermostats.
7. **Pump-block test:** close all thermostats with supervision, record coil
   counts and water ΔT, then reopen. This is a separate, explicitly approved
   test.
8. **Repeats:** at least three sessions per condition; report variability.
9. **Safety:** the tool never exceeds the panel's legal values, never runs
   unattended, and stops on stale water temperature or unexpected pump state.

Mark DHW charges and boiler faults in every run. They are disturbances, not
plant gain.

### 6.5 Identification

Start simple and add complexity only when the data supports it:

- **Static interface map:** quasi-steady `duty → boiler displayed Soll` (and
  water temperatures) with confidence intervals. This is the v1 gate.
- **FOPDT inside one regime:** `T(s)/duty(s) = K · e^(−θs) / (τs + 1)`.
  Estimate gain `K` (expect negative in the heating range: lower duty = higher
  temperature), delay `θ` and time constant `τ`. Do not fit one FOPDT across the
  pump threshold and the ignition threshold; that reports a gain that does not
  exist.
- **Regime detection:** segment into OFF, pump-only, ignition wait, weak fire,
  strong fire, modulation and lockout. Start with thresholds on coil RMS and
  the slope of the measured flow temperature; only then consider a hidden
  Markov model.
- **Hysteresis:** fit separate switching thresholds for rising and falling
  water temperature and for the pump.
- **Start delay:** estimate the empirical distribution of time from demand to
  a positive water-temperature slope. Confound it with `Taktsperre`.
- **Load model:** relate `water_out − water_in` and room temperature to the heat
  drawn, so the same duty is not expected to produce the same result in every
  season.

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
- the lab's first output is the interface card, produced by deterministic code.
  The LLM is optional and off the device.

### 6.7 Outputs

- `model.json`: regimes, parameters, uncertainty, validity range.
- `report.md`: what was tested, what was learned, what remains unknown.
- Plots: duty vs. water temperature, residuals, regime timeline, start-delay
  histogram, pump-current vs. ΔT.
- A short firmware-facing summary: recommended deadband, minimum on/off times,
  and the safe duty range. This summary is reviewed by a human; it is not
  generated into control code automatically.

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
| 1 | Night onset | Flow drops to the night value; no off edge, no burner cycle |
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
| 13 | Interface inverse round trip | `inverse(forward(d))` within tolerance for confirmed points |
| 14 | Command domain | Always 1000 or 100..500 |
| 15 | Foreign writer | `MODE_OVERRIDE`, then bounded return to AUTO |
| 16 | Boot with restored duty | No transmit when `0x280` matches the mode; `PWM_SRC_EEPROM` visible |
| 17 | Seven-day simulation | Bounded demand edges, no invariant violation |

### 7.2 Level 2: closed-loop simulator

Use a simple plant model in `host_harness/src/heater_sim.cpp`:

- first-order water-temperature dynamics with transport delay,
- hysteresis and minimum modulation,
- a 3-minute ignition delay,
- optional load disturbance.

Keep it for state-machine invariants. Stability on the real boiler comes from
shadow mode and the interface map, not from an unidentified FOPDT. A simulator
that the lab has not identified will bless a controller that matches the
simulator.

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
manual path is authoritative.

Agreement between planned and actual duty is the wrong pass/fail: they differ
because a person is holding the slider. Log instead:

- the decision and the reason,
- the suppressed reason (`min_on`, unsynced, NRF yield, `AUTO HOLD`),
- the flow and room traces,
- whether the planned duty stayed inside `100..500` or `1000`,
- the switching rate.

Enable transmit after that review.

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

1. **Interface card** from §4.4, or a written statement that the points were not
   measurable and why. This gates the inverse map.
2. **Pure controller and host tests.** No firmware behaviour change.
3. **Config persistence and UI mode.** Manual transmission only.
4. **Shadow mode.** AUTO computes, logs, does not transmit.
5. **Enable AUTO** with conservative defaults and deliberate takeover tested.
6. **Pump guard** after the coil threshold and pump-switch position are
   recorded.
7. **HTTP observability:** `/heater` JSON with mode, state, setpoints, requested
   duty, reason, remaining minimum times and edge count.
8. **Model lab results** update only the default parameters and the inverse
   map; the state machine and safety rules do not depend on the learned model.
9. **Room trim** and **outdoor economy** are later modes with their own
   parameters and tests.

---

## 10. Risks

| Risk | Mitigation |
| --- | --- |
| Treating the boiler as linear | Scheduled flow, slow moves, interface card, shadow mode, no open-loop guarantee |
| Short cycling | Minimum on/off, 2-edge budget, boiler `Taktsperre` acknowledged; count demand edges, not ignitions |
| AUTO overrides a human | Manual OFF latches; takeover is deliberate; foreign writer gets a timed override |
| Sensor loss causes hunting | HOLD and explicit badge; no undefined backup curve |
| Pump guard false positive | 30 min timer/room-trend recovery, stale-input rejection, pump-switch precondition |
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
| 2 | Day flow, night setback | Start from a confirmed point near 55 °C and a 15 K flow setback, floored at the map. Replace 20.5 / 17.0 until room trim exists. |
| 3 | Minimum on/off and edge budget | Keep 10 / 15 min. Set the budget to 2 per rolling hour so it can fire. Count demand edges, and say so. |
| 4 | Water-out probe | Read the live `0x180` identities. The role seed says the probe is installed; the parent whitepaper says it is not. ΔT stays out of the law until both pipes are fresh measurements. |
| 5 | SHT45 missing | v1 scheduled flow does not need it. If trim is active, hold. No undefined backup curve. |
| 6 | Pump guard | Later. Threshold in ADC counts. Recovery by timer and room trend. Record the pump-switch position first. |
| 7 | Does OFF survive reboot? | Yes. The AVR restores the last duty from EEPROM, and `1000` restores as off. The panel sends `1000` only when `0x280` disagrees. AUTO transmits nothing and displays `PWM_SRC_EEPROM`. |
| 8 | Unsynced clock | Hold. Do not apply the day setpoint. |
| 9 | nRF or any foreign `0x280` | Yield. Timed return to AUTO (`MODE_OVERRIDE`) rather than sticky MANUAL. Manual OFF stays sticky. |
| 10 | Frost while OFF | Manual OFF is absolute in v1; badge "frost protection inactive". A later `off_allows_frost` setting may restore AWT-style frost. |
| 11 | Interface measurement | One session: duty 0, 100, 200, 300, 400, 500, 1000, boiler display, and a defined voltmeter. This is the v1 model-lab gate. |
| 12 | Outdoor sensor | Look for the old AF before buying one. Economy lockout is the feature it unlocks. |
| 13 | Schedule clock | A battery RTC removes the WiFi coupling. Until then, hold across an unsynced boot. |

---

## 12. Review disposition

| Review item | Resolution in this revision |
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
| §8 Definition of done | Amended below (§13) |

---

## 13. Definition of done for the first AUTO release

- The interface card exists, or a written statement says which of the seven
  points were not measurable and why.
- Controller and config compile on device and host.
- All 17 use cases pass, including sanitizer runs.
- Manual OFF and deliberate manual takeover are demonstrated on the bench.
- The OFF badge states whether frost protection is active.
- AUTO shadow mode has run for several days; switching rate and suppressed
  reasons have been reviewed.
- Every AUTO transition is logged with mode, reason, flow setpoint, duty and
  timer state.
- The UI shows mode, source and reason without ambiguity.
- The model lab has produced `model.json` and `report.md` for the interface
  card. "The thermal model is not identified yet" is true but does not gate the
  first release.

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
- `src/haus_sensor_roles.cpp` seeded water-out ROM.
- `AVR/01_heizung_temp/src/pwm_out.cpp`, `eeprom_log.cpp`, `main.cpp`.
- `test/test_*` host-test pattern.
- `host_harness/` renderer and `dump_2026-10-23/` data.
- Review of 2026-10-23, incorporated in revision 1.1. The file was removed.
