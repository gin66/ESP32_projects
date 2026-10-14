#include <Arduino.h>
#include <avr/wdt.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <RF24.h>
#include "nrf_payload.h"
#include "haus_can.h"
#include "can_sender.h"
#include "pwm_out.h"
#include "adc_monitor.h"

// ─── Hardware pins (rf-nano V1.0: CE=10, CSN=9) ─────────────────────────────
#define ONE_WIRE_BUS       4   // DS18B20 data on D4
#define RF24_CE_PIN       10   // nRF24 CE (D10)
#define RF24_CSN_PIN       9   // nRF24 CSN (D9)

// CAN_QUIET_RADIO and CAN_LISTEN_ONLY live in can_sender.h so the driver and
// this file cannot disagree.

// ─── Spare I/O ───────────────────────────────────────────────────────────────
// D7 carries software PWM from Timer1 (see pwm_out.cpp for why it is not a
// hardware PWM pin). A0 takes the current-coil signal and is measured by the
// adc_monitor module, which owns the channel selection. D3, D5, D6 and D7 are
// the only unassigned digital pins on the Nano.
#define PWM_DUTY_PERMILLE  1000   // GPIO held high → heater off until a CAN command

// Duty forced onto the output when the PWM ISR is found stalled, and the duty
// the board wakes up with. Deliberately the same value for now.
//
// The README's own measurement says the driver stage inverts, so "higher duty"
// may mean "less heat" — but it also says the probe was on a downstream node
// whose characteristics were never documented. Until that is resolved, forcing
// the *boot* value is the only choice that cannot introduce a direction nobody
// has verified. It guarantees a defined state rather than "whatever interior
// duty happened to be running", which is what the stall otherwise leaves.
//
// Change this in one place once §7 Batch 1.2 has verified the polarity.
#define PWM_FAULT_SAFE_DUTY  PWM_DUTY_PERMILLE

// How long the Timer1 compare vectors may make no progress before it is called
// a stall. At 1 kHz there are two ISR entries per millisecond, so 250 ms is
// ~500 missed entries — far outside anything attributable to a long blocking
// call in this loop (the longest is listen_window(), exactly 250 ms, and that
// polls the liveness itself).
static const unsigned long PWM_STALL_LIMIT_MS = 250UL;

// ─── Cadence ─────────────────────────────────────────────────────────────────
// requestTemperatures() blocks until the DS18B20 conversion finishes (up to
// 750 ms at 12-bit resolution). The radio then turns around and listens for a
// duty command, so each cycle is: broadcast, listen, gap.
static const unsigned long TEMP_PERIOD_MS     = 5000UL;
static const unsigned long IDENTITY_PERIOD_MS = 60000UL;
static const unsigned long IDENTITY_BURST_MS  = 200UL;
static const unsigned long IDENTITY_BOOT_MS   = 15000UL;
static const unsigned long LISTEN_WINDOW_MS   = 250UL;

// CAN health is reported more slowly than the ADC window. Bus faults and queue
// saturation are not per-second events, and EFLG is read-to-clear, so sampling
// faster mostly buys a shorter window in which a transient fault is invisible.
static const unsigned long STATS_PERIOD_MS     = 5000UL;

// DS18B20 conversion time at 12-bit resolution is 750 ms worst case; the
// request/read pair is split across loop iterations so the loop stays responsive.
static const unsigned long CONVERSION_MS      = 800UL;

// ─── 1-Wire / Dallas Temperature ─────────────────────────────────────────────
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// Sorted by ROM address; the index is the sensor's rank and therefore the
// rank that maps onto the CAN identifier.
DeviceAddress sensor_roms[NRF_MAX_SENSORS];
uint8_t sensor_count = 0;

// ─── nRF24 radio ─────────────────────────────────────────────────────────────
// D9 is active low. Output, held high, so the nRF is not selected.
void nrf_csn_high()
{
    digitalWrite(RF24_CSN_PIN, HIGH);
    pinMode(RF24_CSN_PIN, OUTPUT);
}

RF24 radio(RF24_CE_PIN, RF24_CSN_PIN);

// ─── Convert 1/128°C raw to 1/100°C (c°) — integer only ────────────────────
static int16_t raw_to_cdeg(int32_t raw)
{
    // DallasTemperature reports three distinct negative failures. Only
    // DEVICE_DISCONNECTED_RAW used to be mapped; power-on-reset and
    // insufficient-power both fell through the sign branch and went out as
    // large negative temperatures instead of the error sentinel.
    if (raw == DEVICE_DISCONNECTED_RAW ||
        raw == DEVICE_POWER_ON_RESET_RAW ||
        raw == DEVICE_INSUFFICIENT_POWER_RAW) {
        return ERR_FLAG;
    }

    bool neg = (raw < 0);
    uint16_t abs_raw = (uint16_t)(neg ? -raw : raw);
    uint16_t whole     = abs_raw / 128;
    uint8_t  frac_128  = abs_raw % 128;

    // fractional 1/128 → 1/100:  frac_100 = frac_128 * 32 / 25
    uint16_t frac_100 = (frac_128 * 32) / 25;

    int16_t cdeg = (int16_t)(whole * 100 + frac_100);
    return neg ? -cdeg : cdeg;
}

// ─── Compare two ROM addresses (lexicographic) ───────────────────────────────
static int rom_compare(const uint8_t a[8], const uint8_t b[8])
{
    for (uint8_t i = 0; i < 8; i++) {
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}

// ─── PWM duty: commanded from CAN, mirrored on CAN and the radio ────────────
static uint16_t pwm_duty = 0;

// Listen-window accounting, so a failing command path can be localised to
// "never arrived" vs "arrived but malformed" vs "accepted".
static uint16_t win_frames = 0;   // anything received while listening
static uint16_t win_cmds = 0;     // well-formed duty commands accepted
static uint16_t win_bad = 0;      // foreign magic or bad CRC

// ─── Peripheral bring-up state machine ───────────────────────────────────────
// The radio and the CAN controller are brought up from loop(), not setup().
//
// The reason is a boot-path hang: RF24::begin() returns false if the part does
// not answer one SPI readback, and the obvious handling for that was to spin
// forever. On a mains-adjacent heating controller that is the worst possible
// place to hang — not merely because the node then stops, but because the hang
// used to happen *before* pwm_out_begin(), leaving D7 high-impedance and the
// driver stage input floating. A floating input is an undefined output, not a
// safe one.
//
// So setup() now does nothing that can fail before the output is pinned, and
// both peripherals are attempted here instead — retried until they answer. A
// peripheral that is slow at boot, marginal, or not populated at all costs
// telemetry, not the node.

static const unsigned long PERIPH_RETRY_MS = 5000UL;

#if !CAN_QUIET_RADIO
static bool          radioReady     = false;
static unsigned long radioRetryMs   = 0;
static uint16_t      radioInitFails = 0;

// Single entry point for every transmission, so "the radio is not up yet"
// cannot be forgotten at a call site — writing to an un-initialised nRF24
// leaves the shared SPI bus in an unknown state.
static bool radio_send(const void* frame, uint8_t len)
{
    if (!radioReady) return false;
    return radio.write(frame, len);
}

static bool radio_bring_up()
{
    if (!radio.begin()) return false;

    radio.setChannel(NRF_CHANNEL);
    radio.setDataRate(RF24_250KBPS);
    radio.setPALevel(RF24_PA_MAX);
    radio.setPayloadSize(NRF_FRAME_SIZE);   // one length for every frame
    radio.openWritingPipe(NRF_BC_ADDR);      // broadcasts leave on pipe 0
    radio.openReadingPipe(1, NRF_CMD_ADDR);  // duty commands arrive on pipe 1
    radio.setCRCLength(RF24_CRC_16);
    radio.setAutoAck(true);
    radio.setRetries(5, 15);
    radio.stopListening();

#if defined(FAILURE_HANDLING)
    // RF24's own "hardware failure" latch. Cleared on a successful bring-up so
    // that the next fault is attributable to the next failure rather than
    // inherited from this one.
    radio.failureDetected = 0;
#endif
    return true;
}
#endif  // !CAN_QUIET_RADIO

static unsigned long canRetryMs   = 0;
static uint16_t      canInitFails = 0;

// Identity cadence lives at file scope rather than inside loop() so that a
// radio which comes up late can be handed a fresh burst of identity frames —
// otherwise a receiver powered on after the initial burst would wait out the
// full 60 s before learning which sensors exist.
static unsigned long lastIdMs    = 0;
static unsigned long idPeriodMs = IDENTITY_BURST_MS;

// ─── Restart counter ─────────────────────────────────────────────────────────
// Retained across every reset except power-on. The brownout-reset loop is the
// nastiest amplifier of the boot-state problem: the node re-initialises
// repeatedly, re-applying the boot duty each time, and looks exactly like a
// healthy idle node over any link that is not the serial port. Reporting how
// many times this board has restarted is what makes it visible.
//
// PORF distinguishes the two cases that matter: a genuine first power-up, where
// the .noinit contents are meaningless and must be ignored, versus everything
// else — watchdog, brownout, external reset — where the retained value is real.
//
// Note what this does NOT yet cover: the Nano's default fuses have the brownout
// detector disabled, so an undervoltage dip does not produce a flagged reset of
// its own — it produces the erratic behaviour that follows one. Enabling BOD in
// the fuse settings is what would turn a slow brownout into a clean counted
// restart. That is a hardware change, not a firmware one, and it is not done
// here.
static uint8_t bootCount __attribute__((section(".noinit")));

static void peripherals_tick()
{
#if !CAN_QUIET_RADIO
    if (!radioReady) {
        // Rollover-safe deadline test: negative means "not due yet".
        if ((long)(millis() - radioRetryMs) < 0) return;
        radioRetryMs = millis() + PERIPH_RETRY_MS;

        if (radio_bring_up()) {
            radioReady     = true;
            radioInitFails = 0;
            Serial.println(F("[TX] radio ready — 250 kbps, 13-byte frames"));
            // Re-announce the sensor set now that there is a radio to hear it.
            idPeriodMs = IDENTITY_BURST_MS;
        } else {
            radioInitFails++;
            // Report the first failure and then every twelfth: an absent radio
            // is a wiring state, not an event, and must not flood the log.
            if (radioInitFails == 1 || (radioInitFails % 12) == 0) {
                Serial.print(F("[TX] nRF24 not responding (attempt "));
                Serial.print(radioInitFails);
                Serial.println(F(") — continuing without radio"));
            }
        }
        return;
    }

#if defined(FAILURE_HANDLING)
    // Up and running. RF24 raises failureDetected when a transfer never
    // completes at all. That is distinct from a frame simply not being
    // acknowledged: a frame to an absent receiver still ends in TX_DF after
    // the retry count expires, which is normal, not a fault. Only the former
    // means the radio has stopped answering.
    if (radio.failureDetected) {
        radioReady = false;
        Serial.println(F("[TX] radio stopped answering — re-initialising"));
    }
#endif
#endif  // !CAN_QUIET_RADIO

    // CAN answers from can_present(), which is the driver's own ready flag —
    // one source of truth, no second copy to drift.
    if (!can_present()) {
        if ((long)(millis() - canRetryMs) < 0) return;
        canRetryMs = millis() + PERIPH_RETRY_MS;

        if (can_init()) {
            canInitFails = 0;
            Serial.print(F("[TX] CAN ready @ 125 kbit/s, data id "));
            Serial.print(CAN_ID_DATA_BASE, HEX);
            Serial.println(F(""));
            can_dump_config();
        } else {
            canInitFails++;
            if (canInitFails == 1 || (canInitFails % 12) == 0) {
                Serial.print(F("[TX] CAN controller not answering (attempt "));
                Serial.print(canInitFails);
                Serial.println(F(") — continuing without CAN"));
            }
        }
    }
}

static void broadcast_pwm()
{
    PwmFrame f;
    f.magic  = NRF_MAGIC_PWM;
    f.duty   = pwm_duty;
    f.hz     = PWM_OUT_HZ;
    f.flags  = pwm_out_active() ? PWM_FLAG_ACTIVE : 0;
    memset(f.reserved, 0, sizeof(f.reserved));
    f.crc    = frame_crc16(&f, sizeof(f));

#if !CAN_QUIET_RADIO
    radio_send(&f, sizeof(f));
#endif
#if !CAN_LISTEN_ONLY
    can_send_pwm(f.duty, f.hz, f.flags);
#endif
}

// Last writer wins: no ramping, no minimum-on/off pulse, no other validation
// than the permille clamp inside pwm_out_set().
static void set_pwm_duty(uint16_t permille)
{
    pwm_out_set(permille);
    pwm_duty = permille;
    broadcast_pwm();
}

// ─── PWM ISR liveness ────────────────────────────────────────────────────────
// A watchdog fed unconditionally at the bottom of loop() proves one thing only:
// that the main loop is turning. It cannot see a dead Timer1 compare vector.
// If the compare interrupts stop while the main loop keeps spinning, the WDT
// keeps being fed happily while the duty output has silently frozen — and that
// output is the thing that drives the heater. An unconditional feed would report
// that node as perfectly healthy.
//
// So edge progress is checked separately, and deliberately does NOT reset the
// board. A stalled PWM is a partial fault, and a reset would drop the output
// straight onto PWM_DUTY_PERMILLE — the unsafe boot state — turning a partial
// fault into a total one. Instead it forces a defined safe duty, latches a flag
// for the stats frame, and counts itself.
static bool          pwmStalled    = false;
static uint32_t      pwmStallCount = 0;
static uint32_t      pwmLastRuns   = 0;
static unsigned long pwmLastOkMs   = 0;

static void pwm_liveness_tick()
{
    uint32_t runs = pwm_out_isr_runs();
    unsigned long now = millis();

    if (runs != pwmLastRuns) {
        pwmLastRuns = runs;
        pwmLastOkMs = now;
        if (pwmStalled) {
            pwmStalled = false;
            Serial.println(F("[TX] PWM ISR recovered"));
        }
        return;
    }

    // At 0 or 1000 permille both interrupts are deliberately off and the port is
    // driven directly — there are no edges to make. Demanding them there would
    // report a fault that does not exist, and PWM_DUTY_PERMILLE is the default
    // boot state, so that guard is what stops this firing forever at power-on.
    if (!pwm_out_active()) return;

    // Grace after TIMSK1 is first set: the first compare match has not landed yet.
    if ((unsigned long)(now - pwmLastOkMs) < PWM_STALL_LIMIT_MS) return;

    if (!pwmStalled) {
        pwmStalled = true;
        pwmStallCount++;
        Serial.println(F("[TX] PWM ISR stalled — forcing safe output"));
        set_pwm_duty(PWM_FAULT_SAFE_DUTY);
    }
}

// ─── Listen window ───────────────────────────────────────────────────────────
// After each broadcast the radio turns around and listens for a duty command.
// A nRF-only controller needs no schedule of its own: it waits for the
// broadcast it was going to receive anyway, and replies to it. Because duty is
// overwrite-only, a controller that misses a window simply retries next cycle,
// so no synchronisation or beacon is required.
//
// Nothing is transmitted from in here; the reply carrying the new duty is
// deferred until the radio is back in transmit mode.
#if !CAN_QUIET_RADIO
// Requires radioReady: startListening()/available()/read() on an un-initialised
// radio reads an unknown SPI bus. Caller guarantees it.
static void listen_window(unsigned long ms)
{
    uint16_t want = pwm_duty;
    bool changed = false;

    radio.startListening();

    unsigned long until = millis() + ms;
    while ((long)(until - millis()) > 0) {
        can_pump();

        // Keep sampling through the listen window. Without this the loop would be
        // blind for the whole 250 ms, which is 25 windows' worth of a 50 Hz signal
        // — and because min/max is an extremum statistic, a gap that long can
        // swallow whole half-cycles and pull the reported peak in.
        adc_monitor_tick();

        // This window is the longest blocking region in the firmware — exactly
        // as long as PWM_STALL_LIMIT_MS. Keeping both checks running inside it
        // means neither has to reason about the other: the stall timer cannot
        // be fooled by this block, and the watchdog is fed on the way through.
        pwm_liveness_tick();
        wdt_reset();

        uint16_t cmd;
        if (can_read_pwm_command(&cmd) && cmd != pwm_duty) {
            want = cmd;
            changed = true;
        }

        if (!radio.available()) continue;

        uint8_t buf[NRF_FRAME_SIZE];
        radio.read(buf, sizeof(buf));

        win_frames++;

        uint16_t magic;
        memcpy(&magic, buf, sizeof(magic));
        if (magic != NRF_MAGIC_PWM_CMD) { win_bad++; continue; }

        PwmFrame f;
        memcpy(&f, buf, sizeof(f));
        if (f.crc != frame_crc16(&f, sizeof(f))) { win_bad++; continue; }

        win_cmds++;
        if (f.duty != pwm_duty) {
            want = f.duty;
            changed = true;
        }
    }

    radio.stopListening();
    radio.openWritingPipe(NRF_BC_ADDR);   // re-assert after the mode switch

    if (changed) {
        set_pwm_duty(want);
        Serial.print("[TX] PWM set to ");
        Serial.print(want);
        Serial.println(" permille");
    }
}
#endif

// ─── Rank sensors by ROM address ─────────────────────────────────────────────
// `i + 1 < count` rather than `i < count - 1`: the count is a uint8_t, so the
// latter underflows to 255 when no sensor is present.
static void sort_by_rom(uint8_t count)
{
    for (uint8_t i = 0; i + 1 < count; i++) {
        for (uint8_t j = i + 1; j < count; j++) {
            if (rom_compare(sensor_roms[j], sensor_roms[i]) < 0) {
                DeviceAddress tmp;
                memcpy(tmp, sensor_roms[i], 8);
                memcpy(sensor_roms[i], sensor_roms[j], 8);
                memcpy(sensor_roms[j], tmp, 8);
            }
        }
    }
}

// ─── Refresh the ranked ROM table from the bus ──────────────────────────────
// Re-enumerating each cycle is what makes rank meaningful: DallasTemperature
// caches the bus scan, so getAddress() costs no 1-Wire traffic. Without this
// the table would be frozen at boot and identity frames would keep announcing
// a sensor that had since been unplugged.
static void scan_sensors()
{
    uint8_t count = sensors.getDeviceCount();
    if (count > NRF_MAX_SENSORS) count = NRF_MAX_SENSORS;

    for (uint8_t i = 0; i < count; i++) {
        sensors.getAddress(sensor_roms[i], i);
    }
    sort_by_rom(count);
    sensor_count = count;
}

// ─── Announce every sensor's ROM on its own frame ───────────────────────────
static void send_identities()
{
    for (uint8_t i = 0; i < sensor_count && i < NRF_MAX_SENSORS; i++) {
        IdFrame f;
        f.magic = NRF_MAGIC_ID;
        f.rank  = i;
        memcpy(f.rom, sensor_roms[i], 8);
        f.crc = frame_crc16(&f, sizeof(f));

#if !CAN_QUIET_RADIO
        radio_send(&f, sizeof(f));
#endif
#if !CAN_LISTEN_ONLY
        can_send_identity(i, sensor_roms[i]);
#endif
    }
}

// ─── Broadcast all readings in one frame ────────────────────────────────────
static bool send_temperatures(TempFrame& f)
{
    scan_sensors();

    int32_t raw_vals[NRF_MAX_SENSORS] = { 0 };
    for (uint8_t i = 0; i < sensor_count; i++) {
        raw_vals[i] = sensors.getTemp(sensor_roms[i]);
    }

    // No re-sort needed: scan_sensors() left the table in rank order, and each
    // reading was fetched by the address at its own index.
    f.magic   = NRF_MAGIC_TEMP;
    f.present = 0;

    for (uint8_t i = 0; i < NRF_MAX_SENSORS; i++) {
        if (i < sensor_count) {
            f.temp[i] = raw_to_cdeg(raw_vals[i]);
            f.present |= TEMP_PRESENT_BIT(i);
        } else {
            f.temp[i] = ERR_FLAG;
        }
    }
    f.crc = frame_crc16(&f, sizeof(f));

#if !CAN_QUIET_RADIO
    // False covers both "the radio is not up" and "no node acknowledged" —
    // both mean the readings did not leave this board.
    bool ok = radio_send(&f, sizeof(f));
#else
    (void)f;
    bool ok = true;
#endif

#if !CAN_LISTEN_ONLY
    can_send_temperatures(f.temp);
#endif
    return ok;
}

// ─── Publish one ADC window ──────────────────────────────────────────────────
// The window is min/max, not an average, because the input is a 50 Hz current
// coil and the interesting quantity is the envelope. A mean of a sine wave would
// report a number that moves with the load but not with the current amplitude.
static void send_adc_stats()
{
    AdcStats s;
    if (!adc_monitor_take(&s)) return;   // no readings at all — say nothing

    AdcFrame f;
    f.magic  = NRF_MAGIC_ADC;
    f.max    = s.max;
    f.min    = s.min;
    f.pp     = (uint16_t)(s.max - s.min);
    f.flags  = s.flags | ADC_FLAG_INTERNAL_REF;
    // Saturate rather than wrap: a wrapped count would read as a nearly empty
    // window, which is the opposite of what an unusually busy loop means.
    f.readings = (uint16_t)(s.readings > 0xFFFFUL ? 0xFFFF : s.readings);
    f.crc    = frame_crc16(&f, sizeof(f));

#if !CAN_QUIET_RADIO
    radio_send(&f, sizeof(f));
#endif
#if !CAN_LISTEN_ONLY
    can_send_adc(f.max, f.min, f.pp, f.flags);
#endif

    // The mean is here to distinguish a quiet signal from a noisy one: pp on its
    // own cannot tell a stable DC level from an AC signal with the same span.
    uint16_t mean = (uint16_t)(s.sum / (s.readings ? s.readings : 1));

    Serial.print("[TX] adc  min=");
    Serial.print(f.min);
    Serial.print(" max=");
    Serial.print(f.max);
    Serial.print(" pp=");
    Serial.print(f.pp);
    Serial.print(" mean=");
    Serial.print(mean);
    if (f.flags & ADC_FLAG_RAILED) {
        Serial.print(" RAILED");
    }
    Serial.print("  n=");
    Serial.println(f.readings);
}

// ─── Publish CAN bus health ──────────────────────────────────────────────────
// Sent over the radio as well as CAN, so the bus can be watched wirelessly. The
// frame reports counters since boot plus the live register values; it is sent on
// CAN too, so a sniffer sees the same thing a radio listener does.
static void send_can_stats()
{
    uint16_t sent = 0, recv = 0, refused = 0;
    uint8_t  eflg = 0, tec = 0, rec = 0, intf = 0;
    can_stats(&sent, &recv, &refused, &eflg, &tec, &rec, &intf);

    // Asked before building the frame: the flag is about the module's own state,
    // and it has to be true in the radio frame whether or not CAN itself is up.
    bool present = can_present();

    StatsFrame f;
    f.magic    = NRF_MAGIC_STATS;
    f.sent     = sent;
    f.received = recv;
    f.refused  = refused;
    f.eflg     = eflg;
    f.canstat  = 0;   // CANSTAT is not in the library's public API
    f.present  = present ? NRF_STATS_FLAG_PRESENT : 0;
    f.crc      = frame_crc16(&f, sizeof(f));

    // Written even when the controller is absent. With no MCP2515 the CAN frame
    // is a no-op, but the radio frame still goes out and still carries the
    // absence — which is the case that most needs to be visible.
#if !CAN_QUIET_RADIO
    radio_send(&f, sizeof(f));
#endif
#if !CAN_LISTEN_ONLY
    can_send_stats(sent, recv, refused, eflg, 0, f.present);
#endif

    // Printed whether or not the controller answered: with it absent the register
    // values are zero but the software counters (sent/recv/refused/bad) still
    // move, and seeing them next to "ABSENT" is what separates a silent controller
    // from a silent bus.
    Serial.print(F("[TX] can "));
    if (!present) Serial.print(F("ABSENT "));

    Serial.print("sent=");
    Serial.print(sent);
    Serial.print(" recv=");
    Serial.print(recv);
    Serial.print(" refused=");
    Serial.print(refused);
    // Non-zero here means frames are arriving damaged rather than merely often.
    Serial.print(" bad=");
    Serial.print(can_rx_malformed());
    Serial.print("  ovf=");
    Serial.print(can_rx_overflows());
    Serial.print("  tec=");
    Serial.print(tec);
    Serial.print(" rec=");
    Serial.print(rec);
    Serial.print(" eflg=0x");
    if (eflg == 0) {
        Serial.print("0");
    } else {
        Serial.print(eflg, HEX);
    }
    // Names follow the datasheet thresholds. EFLG 0x3 with REC 3 is not
    // RX-WARN: that bit is legal only once REC has reached 96.
    if ((eflg & CAN_EFLAG_TXBO)  && tec == 255) Serial.print(F(" TX-BUSOFF"));
    if ((eflg & CAN_EFLAG_TXEP)  && tec >= 128) Serial.print(F(" TX-ERRPASSIVE"));
    if ((eflg & CAN_EFLAG_TXWAR) && tec >= 96)  Serial.print(F(" TX-WARN"));
    if ((eflg & CAN_EFLAG_RXEP)  && rec >= 128) Serial.print(F(" RX-ERRPASSIVE"));
    if ((eflg & CAN_EFLAG_RXWAR) && rec >= 96)  Serial.print(F(" RX-WARN"));
    if (eflg & CAN_EFLAG_RX0OVR) Serial.print(F(" RXBUF0-OVR"));
    if (eflg & CAN_EFLAG_RX1OVR) Serial.print(F(" RXBUF1-OVR"));
    Serial.print(" intf=0x");
    Serial.print(intf, HEX);
Serial.print(" boot=");
    Serial.print(bootCount);
    Serial.print(" pwm_stall=");
    Serial.print(pwmStallCount);
    Serial.println(pwmStalled ? F(" ACTIVE") : F(""));
    Serial.print(" rst=");
    Serial.print(can_recoveries());
    Serial.print("/");
    Serial.print(can_recover_tries());
    Serial.print(" abort=");
    Serial.print(can_tx_aborts());
    if (can_tx_aborts() > 0) {
        Serial.print("(0x");
        Serial.print(can_tx_abort_faults(), HEX);
        Serial.print(")");
    }
    Serial.print(" spi=");
    Serial.print(can_spi_errors());
    Serial.print(" probe=");
    // The count alone is the same for 13 zeros, 13 ones, and a shifted
    // pattern. The bytes are what separate those.
    uint8_t probeGot[13];
    uint8_t probeBad = can_spi_probe(probeGot);
    Serial.print(probeBad);
    if (probeBad != 0) {
        Serial.print(F(" read="));
        for (uint8_t i = 0; i < 13; i++) {
            Serial.print(' ');
            if (probeGot[i] < 0x10) Serial.print('0');
            Serial.print(probeGot[i], HEX);
        }
    }
    Serial.println();
}

// ─── Setup ───────────────────────────────────────────────────────────────────
void setup()
{
    // THE FIRST STATEMENT. Unconditional, and not guarded on a reset-cause flag.
    //
    // The ATmega328P watchdog enable latch survives every reset except
    // power-on, and comes back at the fastest prescaler — about 15 ms —
    // regardless of the WDP that was programmed. So this call has to fit inside
    // 15 ms, not inside the WDTO_2S window configured further down.
    //
    // Skip it and the firmware bricks itself on the first non-power-on reset:
    // sensors.begin() alone spends 50–150 ms in delay(), so the board never
    // reaches loop(), never feeds the watchdog, and resets every 15 ms
    // forever. It looks fine during testing because power-on clears the latch —
    // the failure only appears at the first brownout or power glitch.
    //
    // A guard here (`if (MCUSR & _BV(WDRF))`) would reintroduce exactly the
    // state-dependent path this avoids. The call is a handful of cycles; do it
    // unconditionally.
    wdt_disable();

    // Restart accounting. PORF is the only reset this board cannot distinguish
    // from a first-ever power-up, and it is the only case where the .noinit
    // contents are meaningless.
    if (MCUSR & _BV(PORF)) {
        bootCount = 1;
    } else {
        bootCount = (bootCount == 0xFF) ? 1 : (uint8_t)(bootCount + 1);
    }

    // CSN lines first: deselect both SPI peripherals so that nothing on the
    // shared bus is addressed by an accident of pin state after reset.
    can_csn_high();
    nrf_csn_high();

    // The output goes into a defined state next, before anything that can fail
    // or block. Everything below can take hundreds of milliseconds — with the
    // watchdog already running at 2 s, and with the heater's state decided
    // here rather than by whatever the driver stage does with a floating pin.
    pwm_out_begin();
    set_pwm_duty(PWM_DUTY_PERMILLE);

    // 2 s from here on. The longest single blocking region in the loop is
    // listen_window() at 250 ms, which feeds the watchdog itself, so this has
    // roughly 8x headroom even with the RC oscillator's ~10% error.
    wdt_enable(WDTO_2S);

    Serial.begin(115200);
    Serial.println("[TX] boot");

    sensors.begin();
    sensors.setWaitForConversion(false);   // conversion is handled in loop()
    scan_sensors();

    Serial.print("[TX] found ");
    Serial.print(sensor_count);
    Serial.println(" DS18B20(s), ranked by ROM:");
    for (uint8_t i = 0; i < sensor_count; i++) {
        Serial.print("  rank ");
        Serial.print(i);
        Serial.print(" → ");
        Serial.print(rom_to_str(sensor_roms[i]));
        Serial.print("   CAN id 0x");
        Serial.println(CAN_ID_ID_BASE + i, HEX);
    }

    // The radio and the CAN controller are brought up from loop() and retried
    // until they answer. Nothing left in setup() can stop the node.

    Serial.print("[TX] t1 A=");
    Serial.print(pwm_out_tccr1a(), HEX);
    Serial.print(" B=");
    Serial.print(pwm_out_tccr1b(), HEX);
    Serial.print(" ICR1=");
    Serial.print(pwm_out_icr1());
    Serial.print(" OCR1A=");
    Serial.print(pwm_out_ocr1a());
    Serial.print(" OCR1B=");
    Serial.print(pwm_out_ocr1b());
    Serial.print(" TCNT1=");
    Serial.print(TCNT1);
    Serial.println(F("  timer on, D7 driven"));

    // Started last: it rewrites ADMUX/ADCSRA, so anything reading the ADC
    // through analogRead() afterwards would have to undo that.
    adc_monitor_begin();
    Serial.print("[TX] ADC A0 free-running, internal 1.1 V bandgap, LSB 1074.2 uV");

    // A restart count above 1 with no PORF is the brownout-reset loop. It is
    // called out on the boot line rather than only in the periodic stats,
    // because the whole point is that nobody is watching the serial port.
    if (bootCount > 1) {
        Serial.print(F(" — restarted "));
        Serial.print(bootCount);
        Serial.print(F(" times"));
        Serial.print(MCUSR & _BV(PORF) ? F(" (POR)") : F(" (no POR: brownout/watchdog)"));
    }

#if CAN_QUIET_RADIO
    Serial.println(F(" — radio OFF (CAN_QUIET_RADIO=1)"));
#else
    Serial.println();
#endif
}

// ─── Main loop ───────────────────────────────────────────────────────────────
void loop()
{
    static unsigned long lastCycleMs = 0;
    static unsigned long convStartMs = 0;
    static bool          convBusy = false;
    static unsigned long lastStatsMs = 0;
    static uint32_t okCount = 0, failCount = 0;

    // Brings up the radio and CAN if they are not yet up, and detects a radio
    // that has stopped answering. Runs first so the peripherals are attempted
    // on the very first pass rather than after the first sensor scan.
    peripherals_tick();

    can_pump();

    // One unguarded read of the last completed conversion. Called as early in
    // the pass as possible: the later this sits, the more of the previous pass's
    // time goes unsampled.
    adc_monitor_tick();

    // Health, not just liveness: see the comment at pwm_liveness_tick() for why
    // this is separate from the watchdog feed at the bottom of the loop.
    pwm_liveness_tick();

    if (adc_monitor_due()) {
        send_adc_stats();
    }

    if (millis() - lastStatsMs >= STATS_PERIOD_MS) {
        lastStatsMs = millis();
        send_can_stats();
    }

    // CAN commands are applied verbatim and immediately. Anything else on the
    // bus is ignored, so the board keeps broadcasting on a bus it does not own.
    uint16_t cmd;
    if (can_read_pwm_command(&cmd) && cmd != pwm_duty) {
        set_pwm_duty(cmd);
        Serial.print("[TX] PWM commanded to ");
        Serial.print(cmd);
        Serial.println(" permille");
    }

    unsigned long now = millis();

    // Non-blocking conversion. requestTemperatures() used to block for the full
    // 750 ms conversion, which left the loop unresponsive for most of every
    // cycle; the request and the read are now split across iterations.
    if (!convBusy && now - lastCycleMs >= TEMP_PERIOD_MS) {
        lastCycleMs = now;
        convStartMs = now;
        convBusy = true;
        sensors.requestTemperatures();
    }

    if (convBusy && now - convStartMs >= CONVERSION_MS) {
        convBusy = false;

        TempFrame f;
        bool ok = send_temperatures(f);

        // Repeat the duty every cycle so a receiver powering up late converges
        // on the current value without needing a request/response exchange.
        broadcast_pwm();

#if !CAN_QUIET_RADIO
        // Turn the radio around IMMEDIATELY, before the serial reporting below.
        // Printing costs milliseconds at 115200 baud and the controller is
        // counting from the broadcast it just received — opening the window late
        // is what makes a command get missed.
        if (radioReady) listen_window(LISTEN_WINDOW_MS);
#endif

        Serial.print("[TX]");
        for (uint8_t i = 0; i < NRF_MAX_SENSORS; i++) {
            Serial.print("  t");
            Serial.print(i);
            Serial.print("=");
            Serial.print(cdeg_to_str(f.temp[i]));
        }
        Serial.print("  ok=");
        Serial.print(ok ? "Y" : "N");
        Serial.print("  ok_cnt=");
        Serial.print(okCount);
        Serial.print("  fail_cnt=");
        Serial.print(failCount);
        Serial.print("  can_drop=");
        Serial.print(can_dropped());
        Serial.print("  pwm=");
        Serial.print(pwm_duty);
        Serial.print("  edges=");
        Serial.print(pwm_out_rising_edges());
        Serial.print("/");
        Serial.print(pwm_out_falling_edges());
        Serial.print("/");
        Serial.print(pwm_out_timsk1(), HEX);
        Serial.print("  win[f/c/b]=");
        Serial.print(win_frames);
        Serial.print("/");
        Serial.print(win_cmds);
        Serial.print("/");
        Serial.println(win_bad);

        if (ok) okCount++; else failCount++;
    }

    if (now - lastIdMs >= idPeriodMs) {
        lastIdMs = now;
        send_identities();

        if (now >= IDENTITY_BOOT_MS) {
            idPeriodMs = IDENTITY_PERIOD_MS;
        }
    }

    // Watchdog feed. Deliberately last: it attests that the entire pass
    // completed, which is what makes it worth anything. A single `wdr`
    // instruction, and the loop is a free-running spin, so this costs nothing.
    wdt_reset();
}