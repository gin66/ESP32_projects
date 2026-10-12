#include <Arduino.h>
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
#define PWM_DUTY_PERMILLE  0   // held low until a CAN command

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
    radio.write(&f, sizeof(f));
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
        radio.write(&f, sizeof(f));
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
    bool ok = radio.write(&f, sizeof(f));
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
    radio.write(&f, sizeof(f));
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
    radio.write(&f, sizeof(f));
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
    Serial.print("  rst=");
    Serial.print(can_recoveries());
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
    can_csn_high();
    nrf_csn_high();

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

#if CAN_QUIET_RADIO
    Serial.println("[TX] radio OFF (CAN_QUIET_RADIO=1)");
#else
    if (!radio.begin()) {
        Serial.println("[TX] ERROR: nRF24 not responding!");
        while (1) {}
    }

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

    Serial.println("[TX] radio ready — 250 kbps, 13-byte frames");
#endif

    // can_init() runs regardless of CAN_QUIET_RADIO.
    if (can_init()) {
        Serial.print("[TX] CAN ready @ 125 kbit/s, data id ");
        Serial.print(CAN_ID_DATA_BASE, HEX);
        Serial.println("");
        can_dump_config();
    } else {
        Serial.println("[TX] CAN init failed — continuing without CAN");
    }

    pwm_out_begin();
    set_pwm_duty(PWM_DUTY_PERMILLE);

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
}

// ─── Main loop ───────────────────────────────────────────────────────────────
void loop()
{
    static unsigned long lastCycleMs = 0;
    static unsigned long convStartMs = 0;
    static bool          convBusy = false;
    static unsigned long lastIdMs = 0;
    static unsigned long idPeriodMs = IDENTITY_BURST_MS;
    static unsigned long lastStatsMs = 0;
    static uint32_t okCount = 0, failCount = 0;

    can_pump();

    // One unguarded read of the last completed conversion. Called as early in
    // the pass as possible: the later this sits, the more of the previous pass's
    // time goes unsampled.
    adc_monitor_tick();

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
        listen_window(LISTEN_WINDOW_MS);
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
}