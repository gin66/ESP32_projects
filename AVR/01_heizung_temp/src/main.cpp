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
#include "eeprom_log.h"
#include "eeprom_avr.h"
#include "pwm_fault.h"

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

// ─── Duties for the non-commanded states ─────────────────────────────────────
//
// Three distinct constants rather than one, because they are three distinct
// decisions and aliasing them hides which one is in force:
//
//   PWM_DUTY_PERMILLE          boot. Nothing has asked for anything yet.
//   PWM_FAILOVER_PERMILLE      PWM output path is broken; a software square wave
//                              is driving the pin instead. NOT a commanded duty
//                              and not expressed by the counter at all.
//   (removed) PWM_FAULT_SAFE_DUTY — the old name for "whatever the fault path
//                              forced", which was silently PWM_DUTY_PERMILLE. A
//                              detected output failure and a cold start are
//                              different events and must not read the same.
//
// All three are unverified in *direction*, for the same reason: the README says
// the driver stage inverts and that the probe sat on a downstream node whose
// characteristics were never documented. Until §7 item B1.2 measures the load,
// treat these as three numbers to be set, not three meanings that are known.
   // see pwm_fault.h



// ─── Cadence ─────────────────────────────────────────────────────────────────
// requestTemperatures() blocks until the DS18B20 conversion finishes (up to
// 750 ms at 12-bit resolution). The radio then turns around and listens for a
// duty command, so each cycle is: broadcast, listen, gap.
static const unsigned long TEMP_PERIOD_MS     = 5000UL;
static const unsigned long IDENTITY_PERIOD_MS = 60000UL;
static const unsigned long IDENTITY_BURST_MS  = 200UL;
static const unsigned long IDENTITY_BOOT_MS   = 15000UL;
static const unsigned long LISTEN_WINDOW_MS   = 250UL;

/* Short listen window used while in PWM failover. All diagnostic commands —
 * including the one that revives the output — are delivered inside the window,
 * so suppressing it entirely would leave a node in failover with no escape but
 * a power cycle. 20 ms is ample to receive a command and is 2% of a second.
 *
 * The waveform is not corrupted by this: pwm_out_failover_tick() runs at the top
 * of the loop and the window blocks after it, so the pin simply *holds* its
 * level for the duration. That is a bounded 2% duty error instead of the total
 * loss a 250 ms window would cause, and the load's 15-95 ms smoothing averages
 * what is left. */
static const unsigned long LISTEN_WINDOW_FAILOVER_MS = 20UL;

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

// Where pwm_duty came from, as PWM_SRC_*.
//
// The duty is state, not a lease. Whatever the controller last decided stands
// until something changes it — there is no dead-man timeout, because expiring a
// duty on the grounds that the controller has gone quiet would drop the heat
// every time the controller merely rebooted, which is a far more common event
// than a controller dying while still demanding it.
//
// The trade is accepted explicitly: a duty can outlive its author. That is
// correct for "hold the house at 55%" and wrong only if the controller was
// acting on information that has since changed — which is the controller's
// problem to fix when it returns, not this node's to guess at.
//
// So the source is tracked and reported instead. A node that booted on its
// EEPROM restore and has heard nothing since is then distinguishable from one
// being actively commanded, which is the question a receiver really has.
static uint8_t pwm_source = PWM_SRC_EEPROM;

static const char* pwm_source_str(uint8_t src)
{
    switch (src) {
        case PWM_SRC_CAN:   return "CAN";
        case PWM_SRC_NRF:   return "NRF";
        case PWM_SRC_FAULT: return "FAULT";
        default:            return "EEPROM";
    }
}

// Listen-window accounting, so a failing command path can be localised to
// "never arrived" vs "arrived but malformed" vs "accepted".
static uint16_t win_frames = 0;   // anything received while listening
static uint16_t win_cmds = 0;     // well-formed duty commands accepted
static uint16_t win_bad = 0;      // foreign magic or bad CRC
static uint16_t sysCmds = 0;     // diagnostic commands accepted

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

// ─── Restore-point log state ─────────────────────────────────────────────────
// The log itself lives in EEPROM; these are the runtime facts about it that the
// status frames carry. `epl_restored` is what the output was actually set to at
// boot — the single most important number here, because until it is read back
// from the frame there is no way to tell a restored duty from the boot constant.
static epl_state_t epl_state;
static uint16_t   epl_restored  = 0;
static uint8_t    epl_restore_src = EPL_SRC_VIRGIN;
static uint32_t   epl_writes    = 0;

// Status cadence. Slow on purpose — the log changes every 8 h at most, so a fast
// report would be pure bus traffic. An anomaly overrides this and reports at the
// next stats tick, because that is the one case worth telling someone about
// promptly.
static const unsigned long EE_PERIOD_MS = 60000UL;

// Minimum gap between two stored-duty writes triggered by a duty change. Worst
// case is 24 h of writes per day = 8760/year, still ~11 years on one cell, and
// spread over 256 cells it is irrelevant. Its job is to bound how stale the
// stored value can get, not to save wear.
/* How often the duty in force is re-asserted into the log.
 *
 * Previously there was a 1 h rate limit on duty-change writes, added in 1d1fd57
 * as a "backup for the 8 h heartbeat". The 8 h heartbeat was never implemented,
 * so that number became the only change-triggered write path — and because
 * millis() restarts on boot, its one-hour window opened after *every* boot. A
 * node power-cycled more often than hourly, which is what a marginal supply
 * does, would never persist a commanded duty at all. Bench-confirmed: a duty
 * commanded to 500 shortly after boot was gone after a power cycle and the node
 * came back on the previous value.
 *
 * The policy is now simply what it should have been: one write after the boot
 * scan, and one every 8 h after that. Nothing is written in between, so the
 * cost is exactly two records per 8 h of uptime.
 *
 * The window this leaves is real and worth stating: if the duty changes and the
 * node loses power before the next heartbeat, it restores the previous value.
 * That is up to 8 h of exposure, and it is the price of not writing on change.
 * Raising this to something like 60 s costs at most ~6 writes per cell per day
 * (epl_append advances to the next slot, so wear is already spread 240:1) and
 * shrinks the window to a minute. */
static const unsigned long EE_HEARTBEAT_MS = 28800000UL;   // 8 h

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
static pwm_boot_state_t bootState __attribute__((section(".noinit")));

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

static void set_pwm_duty(uint16_t permille);

static void broadcast_pwm()
{
    PwmFrame f;
    f.magic  = NRF_MAGIC_PWM;
    f.duty   = pwm_duty;
    f.hz     = PWM_OUT_HZ;
    /* Bit 0 says whether the PIN is being modulated, not whether the timer is.
     * Those differ in failover: the counter is stopped, but the pin is being
     * square-waved from the main loop at 1 kHz. Reading the timer's state there
     * reported "endpoint, held" about a pin that was actively switching — a
     * receiver would conclude the output was constant while the load was being
     * driven. pwm_out_active() only answers the question for the timer path. */
    uint8_t modulating = (uint8_t)((pwm_out_active() || pwm_out_failover_active())
                                   ? 1u : 0u);

    /* Bit 0 modulating, bits 1..2 the source. Both go out on the same byte, so
     * the radio and CAN frames cannot disagree about where the duty came from. */
    f.flags  = (uint8_t)((modulating ? PWM_FLAG_ACTIVE : 0u) | pwm_source);
    memset(f.reserved, 0, sizeof(f.reserved));
    f.crc    = frame_crc16(&f, sizeof(f));

#if !CAN_QUIET_RADIO
    radio_send(&f, sizeof(f));
#endif
#if !CAN_LISTEN_ONLY
    can_send_pwm(f.duty, f.hz, f.flags);
#endif
}

// ─── Restore-point log: runtime glue ─────────────────────────────────────────
// eeprom_log is deliberately free of Arduino, clocks and Serial so it can be
// compiled unchanged for the host test. These two functions are the entire
// adapter: they call it once at boot, and append the boot marker.
//
// Every append goes through epl_write() so that epl_writes stays an accurate
// liveness counter and a failed append is visible rather than silent.
static const epl_io_t* epl_io(void)
{
    static const epl_io_t* io;
    static uint8_t ready;

    if (!ready) {
        io = eeprom_log_avr_io();
        ready = 1;
    }
    return io;
}

static void epl_write_boot_marker(void)
{
    /* The marker carries the already-resolved duty, which is what makes the next
     * restart an O(1) restore. Skipped when the newest record is already a
     * marker — otherwise a node stuck in a reset loop would spend every one of
     * its boot cycles rewriting the same record, burning the very wear budget
     * the ring exists to spread out.
     *
     * Anomalous ring: nothing is written. epl_append() refuses in that state,
     * and forcing a write to an unlocatable ring is how a recoverable anomaly
     * becomes a permanently corrupted one. */
    if (epl_state.anomaly) return;
    if (epl_state.count > 0u && epl_state.newest < EPL_SLOTS) {
        epl_raw_t probe;
        epl_rec_t newest;
        uint16_t  base = (uint16_t)(epl_state.newest * EPL_REC_SIZE);
        uint8_t   i;

        for (i = 0u; i < (uint8_t)EPL_REC_SIZE; i++) {
            probe.b[i] = epl_io()->rd(epl_io()->ctx, (uint16_t)(base + i));
        }
        newest = epl_decode(&probe);
        if (newest.valid && (newest.flags & EPL_FLAG_BOOT)) return;   /* already stamped */
    }

    if (epl_append(epl_io(), &epl_state, epl_restored, (uint8_t)EPL_FLAG_BOOT) != 0xFFFFu) {
        epl_writes++;
    }
}

/* True when the ring holds a record flagged EPL_FLAG_PWM_FAIL — i.e. the output
 * path has failed at some point, possibly before this boot and possibly across a
 * power cut, which is exactly the case the .noinit escalation latch cannot
 * cover. Set by epl_restore_boot(), read by the status frames. */
static bool epl_pwm_failed = false;

// Scan, restore, and record why. Called once, before pwm_out_begin().
static void epl_restore_boot(void)
{
    const epl_io_t* io = epl_io();

    epl_scan(io, &epl_state);
    epl_restore_ex(io, &epl_state, &epl_restored, &epl_restore_src);

    /* Look back through the restore window for a recorded PWM failure. A record
     * older than that is not interesting: at an 8 h cadence the window is ~80 h,
     * so anything beyond that is history rather than a current condition. */
    epl_pwm_failed = 0u;
    {
        uint8_t i;
        for (i = 0u; i <= EPL_RESTORE_WINDOW; i++) {
            uint16_t slot = (uint16_t)((epl_state.newest + EPL_SLOTS - i) % EPL_SLOTS);
            epl_raw_t raw;
            uint8_t  j;

            if (epl_state.count == 0u || !epl_in_region(&epl_state, slot)) break;

            for (j = 0u; j < (uint8_t)EPL_REC_SIZE; j++) {
                raw.b[j] = io->rd(io->ctx, (uint16_t)(slot * EPL_REC_SIZE + j));
            }
            if (epl_decode(&raw).valid &&
                (epl_decode(&raw).flags & EPL_FLAG_PWM_FAIL)) {
                epl_pwm_failed = 1u;
                break;
            }
        }
    }

    if (epl_state.anomaly) {
        /* The restore point is gone. This is the one log line that must never be
         * missed, because the node now looks healthy while running on the boot
         * constant — see risk analysis §3.0. */
        Serial.println(F("[TX] EEPROM log ANOMALOUS — restore point lost"));
    }
}

// Last writer wins: no ramping, no minimum-on/off pulse, no other validation
// than the permille clamp inside pwm_out_set().
//
// `src` is one of PWM_SRC_*, recorded so a receiver can tell an actively
// commanded duty from one merely restored. set_pwm_duty() keeps the current
// source, which is what the EEPROM refresh path wants: re-persisting a duty
// does not make EEPROM its author.
static void set_pwm_duty_from(uint16_t permille, uint8_t src)
{
    pwm_source = src;
    set_pwm_duty(permille);
}

static void set_pwm_duty(uint16_t permille)
{
    pwm_out_set(permille);
    pwm_duty = permille;
    broadcast_pwm();

    (void)permille;   /* persistence is the heartbeat's job, not this one's */
}

// ─── PWM ISR liveness ────────────────────────────────────────────────────────
// A watchdog fed unconditionally at the bottom of loop() proves one thing only:
// that the main loop is turning. It cannot see a dead Timer1 compare vector.
// If the compare interrupts stop while the main loop keeps spinning, the WDT
// keeps being fed happily while the duty output has silently frozen — and that
// output is the thing that drives the heater. An unconditional feed would report
// that node as perfectly healthy.
//
// ─── Escalation ──────────────────────────────────────────────────────────────
// pwmStalled means the PWM is not working at all: the Timer1 compare vectors
// have stopped, so the duty in force is not being delivered to the load no
// matter what the status frames claim. That is a hardware-path failure, and it
// escalates in three steps rather than one:
//
//   1. first occurrence -> latch it, note it, and let the watchdog reboot.
//
//      A reset is the right first move, because it can actually fix this:
//      pwm_out_begin() reprograms TCCR1A, ICR1, OCR1A, OCR1B, TCCR1B and TIMSK1
//      from scratch, so anything transient — a lost interrupt, a corrupted
//      timer register, software that clobbered TIMSK1 — is cleared by it.
//
//   2. if the fault is present again on the *next* boot, it is not transient.
//
//   3. safe mode: a software square wave on the pin with the timer stopped.
//      Unavoidable, because if the compare vectors are dead there is no way to
//      make edges with them. See pwm_out_failover_square().
//
// The latch lives in .noinit, not EEPROM. A watchdog reset is not a power-on
// reset, so .noinit survives it, which is all the escalation needs. EEPROM
// would only add durability across a power loss, and a power loss is a fresh
// start anyway — it would spend wear to record a fact nothing acts on.
static pwm_monitor_t pwmMon = { 0u, 0u, 0u };
static uint32_t      pwmStallCount = 0;

// Latched across the watchdog reboot that step 1 triggers. .noinit, not EEPROM:
// a watchdog reset preserves it, and a power loss is a fresh start anyway. The
// EEPROM log carries the fault for *reporting* — see pwm_fault_record().
static pwm_boot_state_t *pwmBoot = &bootState;   /* the .noinit block */

static bool     pwmFailover = false;   // terminal safe mode for this boot
static bool     pwm_fault_persisted = false;  // decided at boot, acted on in loop()



/* Persist the fault into the ring.
 *
 * The .noinit latch is the escalation *decision*, and is cleared by any
 * power-on reset. This is the fault *record*, and it has to be durable: a fault
 * followed by a power blip would otherwise leave no trace, could not be
 * reported, and would reset the escalation history to "first occurrence". */
static void pwm_fault_record(void)
{
    if (epl_state.anomaly) return;   /* never scribble into an unlocatable ring */
    if (epl_append(epl_io(), &epl_state, pwm_duty, (uint8_t)EPL_FLAG_PWM_FAIL) != 0xFFFFu) {
        epl_writes++;
    }
}

static void pwm_failover_enter(void)
{
    pwmFailover = true;
    pwm_source  = PWM_SRC_FAULT;

    /* Publish the duty that is actually reaching the load, not the one that was
     * commanded and could not be delivered. The frame reports the output state,
     * so leaving pwm_duty at the failed command would have every receiver
     * believing the heater was being driven at a duty it is not being driven at.
     * src=FAULT on the same frame says why the value changed and who did it. */
    pwm_duty = PWM_FAILOVER_PERMILLE;

    pwm_fault_record();
    broadcast_pwm();

    Serial.println(F("[TX] PWM FAILURE PERSISTED — entering safe mode"));
    Serial.print(F("[TX]   1 kHz square wave @ "));
    Serial.print(PWM_FAILOVER_PERMILLE);
    Serial.println(F(" permille on D7; node stays alive, telemetry continues"));

    /* NOT a blocking spin. An earlier revision looped here forever, which made
     * safe mode unreachable: no serial, no CAN, no radio, and no way out but a
     * power cycle — on the wire indistinguishable from a crashed node, which is
     * the opposite of what a diagnostic state is for. The waveform is advanced
     * from loop() instead, so the fault stays visible and a bench command can
     * try to revive the output path. */
    pwm_out_failover_begin(PWM_FAILOVER_PERMILLE);
}

static void pwm_liveness_tick(void)
{
    uint32_t runs = pwm_out_isr_runs();
    pwm_fault_action_t act;

    if (!pwm_monitor_observe(&pwmMon, runs, millis(),
                             (uint8_t)(pwm_out_active() ? 1u : 0u))) {
        if (pwmMon.stalled) Serial.println(F("[TX] PWM ISR recovered"));
        return;
    }

    if (pwmStallCount == 0u || (pwmStallCount & 0xFFu) == 0u) {
        Serial.println(F("[TX] PWM ISR stalled — no edge progress"));
    }

    act = pwm_fault_on_stall(&pwmBoot->pwm_fault_last_boot,
                          &pwmBoot->pwm_fault_runs);

    if (act == PWM_FAULT_FAILOVER) {
        pwm_failover_enter();          /* never returns */
    }

    if (act == PWM_FAULT_REBOOT) {
        pwmStallCount++;
        pwm_fault_record();
        Serial.println(F("[TX]   first occurrence — latching and rebooting"));

        /* Reboot by starving the watchdog: stop feeding it and let the 2 s
         * timeout fire. Deliberately not an explicit reset — going through the
         * watchdog path means the retained flags and the timer registers are all
         * re-initialised exactly as on any other cold start, and it exercises
         * the same watchdog that is the whole point of the exercise. */
        for (;;) {
            /* no wdt_reset() */
        }
    }
}

// ─── Diagnostic commands ─────────────────────────────────────────────────────
// Bench-only paths that cannot be produced any other way. The PWM stall in
// particular ends in a watchdog reset, so without these the escalation ladder
// and the reboot path are unobservable from a test bench.
//
// Every one of these is logged loudly before it acts. They add no new exposure:
// the same pipe already accepts a duty command, which controls the heater
// directly, and a command that reboots the node is strictly less consequential
// than one that sets its output.
static void sys_cmd_exec(uint8_t cmd, uint8_t arg)
{
    switch (cmd) {

    case SYSCMD_REBOOT:
        Serial.println(F("[TX] *** DIAG: reboot requested — starving watchdog"));
        Serial.flush();
        /* Deliberately not an explicit reset: going through the watchdog is what
         * this command exists to verify. It also leaves the retained flags and
         * the timer registers initialised exactly as on a cold start. */
        for (;;) {
            /* no wdt_reset() */
        }

    case SYSCMD_PWM_STALL:
        Serial.println(F("[TX] *** DIAG: killing TIMSK1 — expect stall, reboot,"));
        Serial.println(F("[TX] ***       then safe mode on the *next* fault"));
        Serial.flush();
        /* The most direct way to simulate a dead output path: clear the compare
         * enables so no edge is ever generated again, leaving everything else —
         * the main loop, millis(), the watchdog — perfectly healthy. That is
         * the exact condition pwm_monitor_observe() exists to catch, and it
         * cannot be produced by breaking anything physical. */
        TIMSK1 = 0;
        return;

    case SYSCMD_EE_ANOMALY:
        /* Fill EVERY slot with a valid record, so there is no empty arc at all
         * and the newest cannot be located.
         *
         * This was previously an erase, which does the opposite: erasing to 0xFF
         * leaves a 256-slot empty arc and the scan correctly reports VIRGIN. So
         * the command named "anomaly" was exercising the wrong branch entirely.
         *
         * The VALID_MAX guard inside epl_append() makes this state unreachable
         * through the normal path — which is the guard working, not a problem —
         * so the records are written straight through the io interface, the same
         * way the host test builds it. */
        Serial.println(F("[TX] *** DIAG: filling every EEPROM slot"));
        Serial.flush();
        {
            uint16_t slot;
            for (slot = 0u; slot < EPL_SLOTS; slot++) {
                epl_raw_t raw;
                uint16_t base = (uint16_t)(slot * EPL_REC_SIZE);
                uint8_t  j;

                epl_encode(&raw, (uint16_t)(100u + slot), (uint8_t)0);
                for (j = 0u; j < (uint8_t)EPL_REC_SIZE; j++) {
                    epl_io()->wr(epl_io()->ctx, (uint16_t)(base + j), raw.b[j]);
                }
            }
        }
        epl_scan(epl_io(), &epl_state);
        if (epl_state.anomaly) {
            Serial.println(F("[TX] ***       anomaly set — restore falls back to OFF"));
            /* This is destructive on purpose, and the node cannot recover from it
             * by itself: epl_append() refuses in the anomalous state, so the duty
             * stops being persisted entirely and the next power cut would restore
             * OFF. Say so, and say the key. */
            Serial.println(F("[TX] ***       node cannot write the log now — press X to erase it"));
        } else {
            Serial.println(F("[TX] ***       NOT anomalous — check the build"));
        }
        return;

    case SYSCMD_EE_ERASE:
        /* The operator action, and a distinct thing from CLEAR_STATS: this is
         * the only command that touches EEPROM on purpose. Normally unnecessary,
         * because bumping EPL_FORMAT invalidates every existing record on its
         * own — this is for a log that has to be discarded right now. */
        Serial.println(F("[TX] *** DIAG: erasing the EEPROM log"));
        Serial.flush();
        {
            /* A full erase is 1024 EEPROM byte writes at ~3.3 ms each, so about
             * 3.4 s. The watchdog fires at 2 s and is not fed anywhere in this
             * loop, so without this the command trips it partway through and
             * reboots the node into a half-erased log — the very state X exists
             * to remove. An earlier run did exactly that and left rec=9 with the
             * fault flag still set, which read as "the erase did nothing".
             *
             * Fed every 16 slots so the margin is comfortable rather than
             * borderline, with progress dots because 3.4 s of silence on a
             * bench looks like a hang. */
            uint16_t slot;
            Serial.print(F("[TX] ***       erasing"));
            for (slot = 0u; slot < EPL_SLOTS; slot++) {
                epl_erase_slot(epl_io(), slot);
                if ((slot & 0x0Fu) == 0x0Fu) {
                    Serial.print('.');
                    Serial.flush();
                    wdt_reset();
                }
            }
            Serial.println();
        }
        epl_scan(epl_io(), &epl_state);
        epl_writes = 0u;

        /* Prove the recovery rather than asserting it: the whole point of X is
         * that the node can write the log again, including after A has wedged it.
         * An erase that left the ring unusable would be worse than none. */
        if (epl_append(epl_io(), &epl_state, epl_restored,
                       (uint8_t)EPL_FLAG_BOOT) != 0xFFFFu) {
            epl_writes++;
            Serial.println(F("[TX] ***       log erased and writable again"));
        } else {
            Serial.println(F("[TX] ***       ERASE FAILED — log still unwritable"));
        }
        return;

    case SYSCMD_REVIVE:
        if (!pwmFailover) {
            Serial.println(F("[TX] *** DIAG: revive — not in failover"));
            return;
        }
        Serial.println(F("[TX] *** DIAG: reviving — handing D7 back to Timer1"));
        Serial.flush();
        /* If the path is genuinely broken the liveness monitor will put it
         * straight back into failover, which is the correct outcome: the attempt
         * is bounded and self-limiting rather than a way to escape a real fault. */
        pwmFailover = false;
        pwm_out_failover_end();
        set_pwm_duty(pwm_duty);
        return;

    case SYSCMD_CLEAR_STATS:
        Serial.println(F("[TX] *** DIAG: clearing counters"));
        Serial.flush();
        win_frames = win_cmds = win_bad = sysCmds = 0u;
        pwmStallCount = 0u;
        return;

    default:
        Serial.print(F("[TX] *** DIAG: unknown command "));
        Serial.println(cmd, HEX);
        return;
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

        if (magic == NRF_MAGIC_SYS) {
            SysCmdFrame c;
            memcpy(&c, buf, sizeof(c));
            if (c.crc == frame_crc16(&c, sizeof(c))) {
                sysCmds++;
                sys_cmd_exec(c.cmd, c.arg);
            } else {
                win_bad++;
            }
            continue;   /* never a duty change */
        }

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
        set_pwm_duty_from(want, PWM_SRC_NRF);
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
    uint8_t sent = 0;

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
        sent++;
    }

    /* Logged because a missing identity frame on the receiving side is otherwise
     * undiagnosable from here: the transmitter would show a normal-looking
     * temperature cycle while the receiver's sensor list stayed empty, and there
     * would be nothing here to tell the two apart. */
    if (sent > 0u) {
        Serial.print(F("[TX] identity burst: "));
        Serial.print(sent);
        Serial.print(F(" sensor(s), rank0="));
        Serial.println(rom_to_str(sensor_roms[0]));
    } else {
        Serial.println(F("[TX] identity burst: NONE — no sensors ranked"));
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

// ─── Publish EEPROM restore-point log status ─────────────────────────────────
//
// The log is what makes the commanded duty survive a power cut, so its health is
// part of the node's state, not a debugging extra. It travels on its own frame
// (CAN 0x2C8, radio 0xABD3) rather than inside the CAN-health frame, because the
// two move on completely different timescales: bus health every few seconds,
// the log every eight hours.
//
// The restore *source* travels with the duty deliberately. "The output came up
// off because that was the decision" and "it came up off because the log was
// unreadable" produce the same duty, and only the source tells them apart —
// which is the difference between a working node and one that has silently lost
// its restore point.
static void send_ee_stats()
{
    EeFrame f;
    uint8_t flags = 0;

    if (epl_state.anomaly) flags |= EE_FLAG_ANOMALY;
    if (epl_state.count == 0) flags |= EE_FLAG_EMPTY;
    /* A PWM failure recorded in the log, which is durable where the .noinit
     * escalation latch is not: it survives a power blip, so a node whose output
     * path failed once still says so after the history has been cleared. */
    if (epl_pwm_failed) flags |= EE_FLAG_PWMFAIL;
    if (epl_state.wrapped)   flags |= EE_FLAG_WRAPPED;
    if (epl_restore_src != EPL_SRC_MARKER && epl_restore_src != EPL_SRC_VIRGIN)
        flags |= EE_FLAG_NOMARK;
    if (epl_restore_src == EPL_SRC_INVALID)   flags |= EE_FLAG_BADNEW;
    if (epl_restore_src == EPL_SRC_ANOMALY)   flags |= EE_FLAG_ANOMALY;

    flags |= (uint8_t)((epl_restore_src << EE_SRC_SHIFT) & EE_SRC_MASK);

    f.magic    = NRF_MAGIC_EE;
    f.restored = epl_restored;
    f.records  = epl_state.count;
    f.writes   = (epl_writes > 0xFFFFu) ? 0xFFFFu : epl_writes;
    f.flags    = flags;
    f.oldest   = (uint8_t)epl_state.oldest;
    f.newest   = (uint8_t)epl_state.newest;
    f.crc      = frame_crc16(&f, sizeof(f));

#if !CAN_QUIET_RADIO
    radio_send(&f, sizeof(f));
#endif

#if !CAN_LISTEN_ONLY
    // Saturating to uint8 on CAN: the ring caps at EPL_VALID_MAX (240) so the
    // record count always fits, and writes-since-boot is a liveness hint that
    // does not need a second byte.
    haus_can_ee_t e;
    e.restored_duty = epl_restored;
    e.flags         = flags;
    e.records       = (epl_state.count > 0xFFu) ? 0xFFu : (uint8_t)epl_state.count;
    e.oldest        = (uint8_t)epl_state.oldest;
    e.newest        = (uint8_t)epl_state.newest;
    e.writes        = (epl_writes > 0xFFu) ? 0xFFu : (uint8_t)epl_writes;
    e.reserved      = 0;
    can_send_ee(&e);
#endif

    Serial.print(F("[TX] ee    restored="));
    Serial.print(epl_restored);
    Serial.print(F("  records="));
    Serial.print(epl_state.count);
    Serial.print(F(" writes="));
    Serial.print(f.writes);
    Serial.print(F("  arc="));
    Serial.print(f.oldest);
    Serial.print(F(".."));
    Serial.print(f.newest);
    switch (epl_restore_src) {
        case EPL_SRC_VIRGIN:  Serial.print(F("  src=virgin"));  break;
        case EPL_SRC_MARKER:  Serial.print(F("  src=marker"));  break;
        case EPL_SRC_WINDOW:  Serial.print(F("  src=window"));  break;
        case EPL_SRC_INVALID: Serial.print(F("  src=BAD-NEWEST")); break;
        case EPL_SRC_ANOMALY: Serial.print(F("  src=ANOMALY-RESTORE-LOST")); break;
        default: break;
    }
    if (epl_state.anomaly) Serial.println(F("  ANOMALOUS"));
    else Serial.println();
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
    // Latched here as well as in the dedicated 0xABD3 frame: an anomaly is
    // the most important thing to know about this node, and it should be
    // visible without decoding a second frame.
    if (epl_state.anomaly) f.present |= NRF_STATS_FLAG_EE_ANOMALY;
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
    Serial.print(bootState.boot_count);
    Serial.print(" pwm_stall=");
    Serial.print(pwmStallCount);
    Serial.println(pwmMon.stalled ? F(" ACTIVE") : F(""));
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
    /* Restart accounting, and the PWM escalation stage.
     *
     * The .noinit block is only trusted if this firmware wrote it — see
     * pwm_boot_state_trusted(). PORF on its own is NOT sufficient, and getting
     * that wrong is what put this node into failover on a freshly flashed chip:
     * an upload starts the application with a watchdog reset, not a power-on
     * one, so PORF stays clear while RAM still holds whatever it powered up as.
     * A garbage run-count above the threshold then escalates before the monitor
     * has run once. */
    if (pwm_boot_state_trusted(&bootState, (uint8_t)((MCUSR & _BV(PORF)) ? 1u : 0u))) {
        bootState.boot_count = (uint8_t)(bootState.boot_count + 1u);

        /* A single latched fault buys a retry, not failover: whether the fault is
         * still there is not knowable until the monitor observes it. */
        if (pwm_fault_boot(&pwmBoot->pwm_fault_last_boot,
                           &pwmBoot->pwm_fault_runs) == PWM_FAULT_FAILOVER) {
            pwm_fault_persisted = 1u;
        }
    } else {
        pwm_boot_state_init(&bootState);
    }

    // CSN lines first: deselect both SPI peripherals so that nothing on the
    // shared bus is addressed by an accident of pin state after reset.
    can_csn_high();
    nrf_csn_high();

    // Restore the duty the upstream controller last commanded, from the EEPROM
    // log. Placed before pwm_out_begin() so the output is driven once, with the
    // restored value, and never blips through the boot constant on the way.
    //
    // The scan reads all 256 slots — about a thousand EEPROM reads, a few
    // milliseconds. That sits inside the 15 ms window the watchdog must be
    // disarmed in only if it happens BEFORE wdt_disable(), so it must stay
    // after it. Hence wdt_disable() above, then this, then the output.
    epl_restore_boot();

    // The output goes into a defined state next, before anything that can fail
    // or block. Everything below can take hundreds of milliseconds — with the
    // watchdog already running at 2 s, and with the heater's state decided
    // here rather than by whatever the driver stage does with a floating pin.
    pwm_out_begin();
    set_pwm_duty_from(epl_restored, PWM_SRC_EEPROM);

    // 2 s from here on. The longest single blocking region in the loop is
    // listen_window() at 250 ms, which feeds the watchdog itself, so this has
    // roughly 8x headroom even with the RC oscillator's ~10% error.
    wdt_enable(WDTO_2S);

    Serial.begin(115200);
    Serial.println("[TX] boot");

    // Re-stamped now that the serial link is up: the boot marker is what makes
    // the next restart an O(1) restore instead of a window walk, so it is worth
    // writing even if this node never runs long enough to hit the 8 h schedule.
    epl_write_boot_marker();

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
    if (bootState.boot_count > 1) {
        Serial.print(F(" — restarted "));
        Serial.print(bootState.boot_count);
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
    static unsigned long lastEeStatsMs = 0;
    static uint8_t       eeAnomalyReported = 0;
    static uint32_t okCount = 0, failCount = 0;

    /* ── Failover output — the first statement of the loop ───────────────────
     *
     * This loop is the timebase for the failover waveform, so the loop's
     * iteration time IS the resolution of the duty reaching the load. Measured on
     * the bench a pass took ~293 us with CAN polling running, which quantises a
     * requested 200 us high time up to a whole pass: the output landed at ~27%
     * instead of 20%, about 6 V rather than 7 V on the measured curve.
     *
     * Worse than the inaccuracy was the coupling: it made the heating output a
     * function of CAN bus load, because a busier bus meant a slower loop meant
     * more duty. pwm_failover_tick() therefore comes before everything, and the
     * expensive per-pass work below is throttled while in failover. What is
     * actually delivered is measured and reported rather than assumed. */
    if (pwmFailover)          pwm_out_failover_tick();
    else if (pwm_fault_persisted) pwm_failover_enter();

    // Brings up the radio and CAN if they are not yet up, and detects a radio
    // that has stopped answering.
    peripherals_tick();

    /* CAN polling is the single largest per-pass cost (three register reads in
     * tx_abort_stuck() plus another three in the RX drain, all SPI). In failover
     * it is polled every eighth pass: the bus is not what failed, the PWM output
     * path is, and a stable waveform is worth more here than bus latency. */
    static uint8_t failoverPoll = 0;
    if (!pwmFailover || (++failoverPoll >= 8u)) {
        if (pwmFailover) failoverPoll = 0u;
        can_pump();
    }

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

    /* The stats report is a long block of Serial at 115200 — tens of
     * milliseconds — which would put a visible dent in a 200 us failover half
     * period. In failover the node keeps running but says less: the fault is
     * already reported on entry and again on the boot line after a restart. */
    if (millis() - lastStatsMs >= STATS_PERIOD_MS) {
        lastStatsMs = millis();
        if (pwmFailover) {
            /* Report the duty actually delivered, not the one requested. They
             * differ because the waveform can only change state at a loop-pass
             * boundary, and the pass length varies with bus and serial load. */
            uint32_t up   = pwm_out_failover_rising();
            uint32_t down = pwm_out_failover_falling();
            uint32_t tot  = up + down;

            Serial.print(F("[TX] failover duty="));
            Serial.print(PWM_FAILOVER_PERMILLE);
            Serial.print(F(" requested, "));
            if (tot) {
                Serial.print((uint32_t)((up * 1000UL) / tot));
            } else {
                Serial.print('?');
            }
            Serial.print(F(" delivered ("));
            Serial.print(up);
            Serial.print('/');
            Serial.print(down);
            Serial.print(F(" edges) boot="));
            Serial.print(bootState.boot_count);
            Serial.println();
        } else {
            send_can_stats();
        }
    }

    /* Restore-point log status. Slow cadence, because the log changes at most
     * every 8 h — but two things jump the queue:
     *
     *   - the first report, which must NOT wait the full period. A node that
     *     booted on a lost restore point would otherwise say nothing about it for
     *     a minute, which is exactly the interval in which the information is
     *     most wanted.
     *   - an anomaly, the one case worth reporting promptly at all.
     */
    /* Re-assert the duty in force, so the persisted value cannot drift
     * arbitrarily far behind the live one. Refused on an anomalous ring:
     * forcing a write into a ring we cannot locate is how a recoverable
     * anomaly becomes a permanent one. */
    static unsigned long lastEeBeatMs = 0u;
    static uint8_t        eeBeatFired = 0u;
    unsigned long         now = millis();
    if ((!eeBeatFired || (unsigned long)(now - lastEeBeatMs) >= EE_HEARTBEAT_MS) &&
        !epl_state.anomaly) {
        if (epl_append(epl_io(), &epl_state, pwm_duty, (uint8_t)0) != 0xFFFFu) {
            epl_writes++;
            lastEeBeatMs = now;
            eeBeatFired = 1u;
            Serial.print(F("[TX] EEPROM heartbeat: duty "));
            Serial.print(pwm_duty);
            Serial.print(F(" permille, records="));
            Serial.print(epl_state.count);
            Serial.print(F(" arc="));
            Serial.print(epl_state.oldest);
            Serial.print(F(".."));
            Serial.println((uint16_t)epl_state.newest);
        }
    }

    static uint8_t eeFirstReported = 0u;
    if (!eeFirstReported ||
        (unsigned long)(millis() - lastEeStatsMs) >= EE_PERIOD_MS ||
        (epl_state.anomaly && !eeAnomalyReported)) {
        lastEeStatsMs = millis();
        eeFirstReported = 1u;
        eeAnomalyReported = epl_state.anomaly ? 1u : 0u;
        send_ee_stats();
    }

    // CAN commands are applied verbatim and immediately. Anything else on the
    // bus is ignored, so the board keeps broadcasting on a bus it does not own.
    uint16_t cmd;
    if (can_read_pwm_command(&cmd) && cmd != pwm_duty) {
        set_pwm_duty_from(cmd, PWM_SRC_CAN);
        Serial.print("[TX] PWM commanded to ");
        Serial.print(cmd);
        Serial.print(" permille  src=");
        Serial.println(pwm_source_str(pwm_source));
    }

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
        /* The window is shortened in failover rather than dropped: it is how
         * commands get in at all, and dropping it was leaving a node in fault
         * with no escape but a power cycle. The pin holds for the duration, so a
         * 20 ms window is a bounded 2% duty error rather than a dead waveform. */
        if (radioReady) listen_window(pwmFailover ? LISTEN_WINDOW_FAILOVER_MS
                                                 : LISTEN_WINDOW_MS);
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
        Serial.print("  sensors=");
        Serial.print(sensor_count);
        Serial.print("  pwm=");
        Serial.print(pwm_duty);
        Serial.print("/");
        Serial.print(pwm_source_str(pwm_source));
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