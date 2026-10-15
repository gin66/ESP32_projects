#include <Arduino.h>
#include <RF24.h>
#include "nrf_payload.h"

// ─── Hardware pins (rf-nano V1.0: CE=10, CSN=9) ─────────────────────────────
#define RF24_CE_PIN       10   // nRF24 CE (D10)
#define RF24_CSN_PIN       9   // nRF24 CSN (D9)

// ─── PWM command, set from the serial console ────────────────────────────────
// Type a duty in permille (0..1000) and press Enter to command the sender's
// output; it is re-sent on every subsequent broadcast until changed.
//
// A controller must NOT reply the instant the broadcast arrives. At that moment
// the sender is still waiting for the auto-ACK and has not yet called
// startListening(); replying immediately lands the command while the sender is
// still a transmitter, and the frame is lost. This delay clears the sender's
// turnaround on both the acknowledged and the unacknowledged path.
static const unsigned long REPLY_DELAY_MS = 50;

// ─── nRF24 radio ─────────────────────────────────────────────────────────────
RF24 radio(RF24_CE_PIN, RF24_CSN_PIN);

static bool     pwm_armed = false;
static uint16_t pwm_target = 0;
static bool     replyPending = false;
static unsigned long replyAtMs = 0;

static char    serialBuf[8];
static uint8_t serialLen = 0;

// ─── Diagnostic commands ─────────────────────────────────────────────────────
// Bench triggers for the failure paths that cannot be produced any other way.
// The reboot and the PWM escalation both end in a watchdog reset, so without
// these they are unobservable from a test bench.
//
// Deliberately radio-only: putting a reboot command on the CAN bus would put it
// on the same wire the safety-critical heating control travels on, for no gain.
/* Transmit and retry until acknowledged, or the deadline passes.
 *
 * The node opens its listen window once per TEMPERATURE cycle — every 5 s — and
 * holds it for 250 ms. So the opportunity arrives every 5000 ms, and a retry span
 * shorter than that has a large chance of containing none at all.
 *
 * An earlier version retried for 1500 ms, reasoned from the 1 s ADC cadence. The
 * ADC frames do go out once a second, but the listen window is not on that clock:
 * it hangs off the conversion-complete branch of the loop, which is every
 * TEMP_PERIOD_MS. A 1500 ms span therefore overlapped a window about a quarter of
 * the time, and three commands in four were dropped in silence.
 *
 * 6 s is the shortest span that guarantees at least one window fits, whatever the
 * phase: up to 250 ms of it is spent waiting inside one.
 *
 * Blocks for the whole deadline, so broadcasts are missed while it runs. That is
 * the right trade for a command somebody just typed by hand.
 *
 * RF24::write() also has to be bracketed by stopListening()/startListening(),
 * or the chip is left in standby-TX and this receiver never hears anything
 * again. */
static bool radio_send_retry(const void* buf, uint8_t len, uint16_t ms)
{
    unsigned long t0 = millis();

    for (;;) {
        radio.stopListening();
        bool ok = radio.write(buf, len);
        radio.startListening();
        if (ok) return true;
        if ((unsigned long)(millis() - t0) >= ms) return false;
        delay(25);
    }
}

static bool sys_cmd_send(uint8_t cmd)
{
    SysCmdFrame f;

    f.magic    = NRF_MAGIC_SYS;
    f.cmd      = cmd;
    f.arg      = 0u;
    memset(f.reserved, 0, sizeof(f.reserved));
    f.crc      = frame_crc16(&f, sizeof(f));

    /* RF24::write() drops the chip into standby-TX (startFastWrite -> ce(HIGH))
     * and never returns it to RX. Calling it while still listening therefore
     * leaves the receiver deaf for the rest of the session — the next
     * diagnostic keypress appears to kill the link, and only a reset brings it
     * back. The duty-send path below already brackets its write for exactly
     * this reason; this one had to as well. */
    return radio_send_retry(&f, sizeof(f), 6000);
}

static void serial_diag(char c)
{
    const char* what;
    uint8_t     cmd;

    switch (c) {
        case 'R': case 'r': what = "reboot (starve watchdog)"; cmd = SYSCMD_REBOOT;      break;
        case 'S': case 's': what = "kill TIMSK1 (PWM stall)"; cmd = SYSCMD_PWM_STALL;   break;
        case 'A': case 'a': what = "fill EEPROM (force anomaly)"; cmd = SYSCMD_EE_ANOMALY;  break;
        case 'X': case 'x': what = "erase EEPROM log";          cmd = SYSCMD_EE_ERASE;    break;
        case 'C': case 'c': what = "clear counters";           cmd = SYSCMD_CLEAR_STATS; break;
        case 'V': case 'v': what = "revive from failover";      cmd = SYSCMD_REVIVE;      break;
        default:
            Serial.println("  ? R=reboot S=pwm-stall A=eeprom-fill X=eeprom-erase C=counters V=revive");
            return;
    }

    Serial.print("  -> DIAG ");
    Serial.print(what);
    Serial.println(sys_cmd_send(cmd) ? " sent" : " (not acked)");
}

// Consume serial input until Enter. Accepts only decimal digits so there is no
// pull-in from random characters, and range-checks before accepting. Letters
// are single-shot diagnostics and take effect immediately.
static void serial_command()
{
    while (Serial.available()) {
        char c = (char)Serial.read();

        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
            serialLen = 0;          // an abandoned number is discarded
            serial_diag(c);
            continue;
        }

        // Accept CR, LF or CRLF. Arduino Serial Monitor can be configured to
        // send any of the three, and treating CR as pure noise means a
        // CR-only host never terminates a line — the digits just pile up.
        // With CRLF, the CR resets the buffer and the LF finds it empty.
        if (c != '\r' && c != '\n') {
            if (c >= '0' && c <= '9' && serialLen < sizeof(serialBuf) - 1) {
                serialBuf[serialLen++] = c;   // no echo: the host provides one
            }
            continue;
        }

        if (serialLen == 0) continue;     // bare Enter

        serialBuf[serialLen] = '\0';
        serialLen = 0;

        unsigned long v = 0;
        bool ok = true;
        for (uint8_t i = 0; serialBuf[i] != '\0'; i++) {
            v = (v * 10) + (unsigned long)(serialBuf[i] - '0');
            if (v > 1000) { ok = false; break; }
        }

        if (!ok) {
            Serial.println("  out of range — use 0..1000");
        } else {
            pwm_target = (uint16_t)v;
            pwm_armed = true;
            Serial.print("  -> commanding ");
            Serial.print(pwm_target);
            Serial.println(" permille");
        }
    }
}

// Send one duty command. Only `duty` is honoured by the sender; `hz` is left
// zero because this node does not own the output's timer.
static void send_pwm_command(uint16_t permille)
{
    PwmFrame f;
    f.magic = NRF_MAGIC_PWM_CMD;
    f.duty  = permille;
    f.hz    = 0;
    f.flags = 0;
    memset(f.reserved, 0, sizeof(f.reserved));
    f.crc   = frame_crc16(&f, sizeof(f));

    bool ok = radio_send_retry(&f, sizeof(f), 6000);

    // A false here means nobody acknowledged: the sender was not inside its
    // listen window when this frame went out, so the command was lost.
    Serial.print("[RX] commanded ");
    Serial.print(permille);
    Serial.print(permille > 0 && !ok ? "  NOT ACKNOWLEDGED — missed the window"
                                     : " permille");
    Serial.println("");
}

// ─── Learned identity table, built at runtime from the identity frames ──────
// The transmitter announces every sensor's ROM on its own frame, repeatedly and
// indefinitely. Nothing is hard-coded here: this table is learned from the air,
// so a receiver that boots at any moment converges on the full set with no
// handshake and no ordering requirement.
static uint8_t known_rom[NRF_MAX_SENSORS][8];
static bool    known[NRF_MAX_SENSORS];

static void note_identity(uint8_t rank, const uint8_t rom[8])
{
    if (rank >= NRF_MAX_SENSORS) return;

    if (!known[rank]) {
        memcpy(known_rom[rank], rom, 8);
        known[rank] = true;
        Serial.print("[RX] learned rank ");
        Serial.print(rank);
        Serial.print(" = ");
        Serial.println(rom_to_str(rom));
        return;
    }

    if (memcmp(known_rom[rank], rom, 8) != 0) {
        // The transmitter re-ranks by sorting whatever sensors are currently
        // present, so removing one shifts every higher rank down. Reporting the
        // old and new ROM makes that observable instead of silent.
        Serial.print("[RX] rank ");
        Serial.print(rank);
        Serial.print(" changed ");
        Serial.print(rom_to_str(known_rom[rank]));
        Serial.print(" -> ");
        Serial.println(rom_to_str(rom));
        memcpy(known_rom[rank], rom, 8);
    }
}

/* ─── Radio bring-up, with retry ───────────────────────────────────────────── */

/* setup() must not hang on a peripheral that failed to answer — the same rule the
 * transmitter follows. On a bench tool an absent radio is fatal to its purpose,
 * so it says so plainly and retries rather than spinning silently. */
static unsigned long radioRetryMs = 0u;
static bool radioReady = false;

static bool radio_bring_up(void)
{
    if (!radio.begin()) return false;

    radio.setChannel(NRF_CHANNEL);
    radio.setDataRate(RF24_250KBPS);
    radio.setPALevel(RF24_PA_MAX);
    radio.setPayloadSize(NRF_FRAME_SIZE);

    // Broadcasts arrive on pipe 1. Commands go out on pipe 0, which
    // openWritingPipe() claims — so reading must not also use pipe 0, since
    // startListening() closes pipe 0 unless it was explicitly opened for RX.
    radio.openWritingPipe(NRF_CMD_ADDR);
    radio.openReadingPipe(1, NRF_BC_ADDR);

    radio.setCRCLength(RF24_CRC_16);
    radio.setAutoAck(true);
    radio.setRetries(5, 15);
    radio.startListening();

#if defined(FAILURE_HANDLING)
    radio.failureDetected = 0;
#endif
    return true;
}

// ─── Setup ───────────────────────────────────────────────────────────────────
void setup()
{
    Serial.begin(115200);
    Serial.println("[RX] boot");

    radioReady = radio_bring_up();
    if (!radioReady) {
        Serial.println(F("[RX] ERROR: nRF24 not responding — retrying every 5 s"));
    }

    Serial.println("[RX] ready — broadcasts on pipe 1, commands on pipe 0");
    Serial.println(F("[RX] keys: 0..1000+Enter=duty   R=reboot S=pwm-stall"));
    Serial.println(F("[RX]       A=eeprom-fill X=eeprom-erase C=counters V=revive"));
    Serial.println("[RX] type a duty 0..1000 and press Enter to command the PWM");
}

static const char* pwm_src_str(uint8_t flags)
{
    switch (flags & PWM_SRC_MASK) {
        case PWM_SRC_CAN:   return "CAN";
        case PWM_SRC_NRF:   return "NRF";
        case PWM_SRC_FAULT: return "FAULT";
        default:            return "EEPROM";
    }
}

// ─── Main loop ───────────────────────────────────────────────────────────────
void loop()
{
    static unsigned long lastStatus = 0;
    static uint16_t rejected = 0;   // frames that failed magic or CRC
    static uint16_t tempFrames = 0;
    static uint16_t idFrames = 0;
    static uint16_t pwmFrames = 0;
    static uint16_t adcFrames = 0;
    static uint16_t statsFrames = 0;
    static uint16_t eeFrames = 0;
    static uint16_t pwm_duty = 0xFFFF;   // no PWM frame seen yet
    static uint8_t  pwm_source = PWM_SRC_EEPROM;
    static uint8_t  pwm_src_have = 0;

    // Last restore-point log report, so the idle summary can say what the
    // transmitter came up with rather than showing nothing until one arrives.
    static bool     ee_have = false;
    static uint16_t ee_records = 0;
    static uint16_t ee_writes = 0;
    static uint8_t  ee_oldest = 0, ee_newest = 0;
    static uint16_t ee_restored = 0;
    static uint8_t  ee_flags = 0;

    /* Last-seen time per frame type.
     *
     * The idle line used to print cumulative counters since boot, which on a
     * quiet link is actively misleading: `adc=36` reads like activity when it
     * may be an hour old. What matters when nothing is arriving is *how long*,
     * not *how many*. Kept as unsigned long so the rollover arithmetic below
     * stays exact. */
    #define RXK_TEMP  0u
    #define RXK_ID    1u
    #define RXK_PWM   2u
    #define RXK_ADC   3u
    #define RXK_STATS 4u
    #define RXK_EE    5u
    #define RXK_COUNT 6u

    static unsigned long seenAt[RXK_COUNT] = { 0 };
    static const char*    seenName[RXK_COUNT] = { "temp", "id", "pwm", "adc", "can-stats", "eeprom" };

    /* How long each frame type may go unseen before it is worth reporting.
     * Per type, because the node does not send everything at one rate: the ADC
     * and PWM frames go out about once a second and temperatures and bus stats
     * every 5 s, but identity and EEPROM status are deliberately slow at 60 s
     * because neither changes quickly. One flat cutoff flagged a perfectly
     * healthy 60 s-cadence frame as stale half the time, which trains you to
     * ignore the one word on the line that is meant to mean something. These
     * mirror the transmitter's own TEMP_PERIOD_MS, IDENTITY_PERIOD_MS,
     * STATS_PERIOD_MS and EE_PERIOD_MS, plus slack for a long loop pass. */
    static const unsigned long staleBudgetMs[RXK_COUNT] = {
        20000UL,    /* temp       5 s  */
        150000UL,   /* id        60 s  */
        10000UL,    /* pwm       ~1 s  */
        10000UL,    /* adc        1 s  */
        20000UL,    /* can-stats  5 s  */
        150000UL,   /* eeprom    60 s  */
    };
    static unsigned long lastFrameMs = 0;
    static unsigned long pwmfailSinceMs = 0;
    static uint8_t      lastKind = 0xFFu;

    // Last CAN stats received, so the idle summary can show current bus health
    // rather than nothing at all between the 5 s reports.
    static bool     stat_have = false;
    static bool     stat_present = false;   // does the transmitter have a controller
static uint8_t  stat_eflg = 0;          // live MCP2515 error flags

    bool gotAny = false;

    while (radio.available()) {
        gotAny = true;

        uint8_t buf[NRF_FRAME_SIZE];
        radio.read(buf, sizeof(buf));

        uint16_t magic;
        memcpy(&magic, buf, sizeof(magic));

        /* Liveness first, before any decode: a frame that fails its CRC is still
         * proof the link is alive, which is the question the idle line answers. */
        {
            uint8_t k = 0xFFu;
            if      (magic == NRF_MAGIC_TEMP)  k = RXK_TEMP;
            else if (magic == NRF_MAGIC_ID)    k = RXK_ID;
            else if (magic == NRF_MAGIC_PWM)   k = RXK_PWM;
            else if (magic == NRF_MAGIC_ADC)   k = RXK_ADC;
            else if (magic == NRF_MAGIC_STATS) k = RXK_STATS;
            else if (magic == NRF_MAGIC_EE)    k = RXK_EE;

            if (k < RXK_COUNT) {
                seenAt[k] = millis();
                lastKind = k;
                lastFrameMs = millis();
            }
        }

        if (magic == NRF_MAGIC_TEMP) {
            TempFrame f;
            memcpy(&f, buf, sizeof(f));

            if (f.crc == frame_crc16(&f, sizeof(f))) {
                tempFrames++;
                Serial.print("[RX]");
                for (uint8_t i = 0; i < NRF_MAX_SENSORS; i++) {
                    if (f.present & TEMP_PRESENT_BIT(i)) {
                        Serial.print("  r");
                        Serial.print(i);
                        Serial.print("=");
                        Serial.print(cdeg_to_str(f.temp[i]));
                    }
                }
                Serial.println("");

                // The temperature broadcast is the sender's once-per-cycle
                // heartbeat, so it is what schedules our reply.
                if (pwm_armed && !replyPending) {
                    replyPending = true;
                    replyAtMs = millis() + REPLY_DELAY_MS;
                }
            } else {
                rejected++;
                Serial.print("[RX] temp rejected: crc got=");
                Serial.print(f.crc, HEX);
                Serial.print(" exp=");
                Serial.println(frame_crc16(&f, sizeof(f)), HEX);
            }

        } else if (magic == NRF_MAGIC_ID) {
            IdFrame f;
            memcpy(&f, buf, sizeof(f));

            if (f.crc == frame_crc16(&f, sizeof(f))) {
                idFrames++;
                note_identity(f.rank, f.rom);
            } else {
                rejected++;
                Serial.print("[RX] identity rejected: crc got=");
                Serial.print(f.crc, HEX);
                Serial.print(" exp=");
                Serial.println(frame_crc16(&f, sizeof(f)), HEX);
            }

        } else if (magic == NRF_MAGIC_ADC) {
            AdcFrame f;
            memcpy(&f, buf, sizeof(f));

            if (f.crc == frame_crc16(&f, sizeof(f))) {
                adcFrames++;
                // Raw counts, deliberately not scaled here: the transmitter
                // cannot vouch for an external reference, so the number that
                // means volts is the one held by whatever drives that reference.
                Serial.print("[RX] adc  min=");
                Serial.print(f.min);
                Serial.print(" max=");
                Serial.print(f.max);
                Serial.print(" pp=");
                Serial.print(f.pp);
                if (f.flags & ADC_FLAG_INTERNAL_REF) Serial.print(" (int 1.1V)");
                if (f.flags & ADC_FLAG_RAILED) {
                    // Full scale somewhere in the window: the peak is clipped, so
                    // max is a floor rather than a measurement.
                    Serial.print(" RAILED");
                }
                Serial.print("  n=");
                Serial.println(f.readings);
            } else {
                rejected++;
                Serial.print("[RX] adc rejected: crc got=");
                Serial.print(f.crc, HEX);
                Serial.print(" exp=");
                Serial.println(frame_crc16(&f, sizeof(f)), HEX);
            }

        } else if (magic == NRF_MAGIC_STATS) {
            StatsFrame f;
            memcpy(&f, buf, sizeof(f));

            if (f.crc == frame_crc16(&f, sizeof(f))) {
                statsFrames++;
                Serial.print("[RX] can  sent=");
                Serial.print(f.sent);
                Serial.print(" recv=");
                Serial.print(f.received);
                Serial.print(" refused=");
                Serial.print(f.refused);

                // Counters are since boot, so a delta between reports is what
                // says anything about current activity. Hold the last values so
                // the summary can print a rate rather than a running total.
                stat_present = (f.present & NRF_STATS_FLAG_PRESENT) != 0;
                stat_have = true;

                Serial.print("  eflg=0x");
                Serial.print(f.eflg, HEX);
                if (f.eflg & CAN_EFLAG_TXBO)   Serial.print(F(" TX-BUSOFF"));
                if (f.eflg & CAN_EFLAG_TXEP)   Serial.print(F(" TX-ERRPASSIVE"));
                if (f.eflg & CAN_EFLAG_TXWAR)  Serial.print(F(" TX-WARN"));
                if (f.eflg & CAN_EFLAG_RXEP)   Serial.print(F(" RX-ERRPASSIVE"));
                if (f.eflg & CAN_EFLAG_RXWAR)  Serial.print(F(" RX-WARN"));
                if (f.eflg & CAN_EFLAG_RX0OVR) Serial.print(F(" RXBUF0-OVR"));
                if (f.eflg & CAN_EFLAG_RX1OVR) Serial.print(F(" RXBUF1-OVR"));

                // Print the raw CANSTAT byte, then decode operation mode (bits 7..5)
                // and interrupt code (bits 3..1).  The four macros below are not
                // MCP2515 CANSTAT bits; they are masked out by mcp.getStatus() and
                // must not be printed here.
                Serial.print(F("  canstat=0x"));
                Serial.print(f.canstat, HEX);
                Serial.print(F(" op="));
                uint8_t op = (f.canstat >> 5) & 0x07;
                switch (op) {
                    case 0: Serial.print(F("normal")); break;
                    case 1: Serial.print(F("sleep"));  break;
                    case 2: Serial.print(F("loopback")); break;
                    case 3: Serial.print(F("listen-only")); break;
                    case 4: Serial.print(F("config")); break;
                    default: Serial.print(op); break;
                }
                Serial.print(F(" icod="));
                uint8_t icod = (f.canstat >> 1) & 0x07;
                Serial.println(icod);
                // Reported last, because it subsumes everything above it: with no
                // controller every counter is zero and no register bit is set.
                if (!(f.present & NRF_STATS_FLAG_PRESENT)) {
                    Serial.println(F("[RX] can  ABSENT — transmitter has no MCP2515"));
                }
            } else {
                rejected++;
                Serial.print("[RX] stats rejected: crc got=");
                Serial.print(f.crc, HEX);
                Serial.print(" exp=");
                Serial.println(frame_crc16(&f, sizeof(f)), HEX);
            }

        } else if (magic == NRF_MAGIC_EE) {
            EeFrame f;
            memcpy(&f, buf, sizeof(f));

            if (f.crc == frame_crc16(&f, sizeof(f))) {
                eeFrames++;
                ee_records = f.records;
                ee_writes  = f.writes;
                ee_oldest  = f.oldest;
                ee_newest  = f.newest;
                ee_flags   = f.flags;
                ee_restored = f.restored;
                ee_have    = true;

                Serial.print(F("[RX] ee   restored="));
                Serial.print(f.restored);
                Serial.print(F("  records="));
                Serial.print(f.records);
                Serial.print(F(" writes="));
                Serial.print(f.writes);
                Serial.print(F("  arc="));
                Serial.print(f.oldest);
                Serial.print(F(".."));
                Serial.print(f.newest);
                if (f.flags & EE_FLAG_WRAPPED) Serial.print(F(" (wrapped)"));

                Serial.print(F("  src="));
                switch (f.flags & EE_SRC_MASK) {
                    case EE_SRC_VIRGIN << EE_SRC_SHIFT: Serial.print(F("virgin"));  break;
                    case EE_SRC_MARKER << EE_SRC_SHIFT: Serial.print(F("marker"));  break;
                    case EE_SRC_WINDOW << EE_SRC_SHIFT: Serial.print(F("window"));  break;
                    case EE_SRC_ANOMALY << EE_SRC_SHIFT: Serial.print(F("ANOMALY")); break;
                    case EE_SRC_INVALID << EE_SRC_SHIFT: Serial.print(F("bad-newest")); break;
                    default: Serial.print(F("?")); break;
                }

                // The one that matters: the duty came up on the boot constant
                // with nothing restored, which looks identical to a deliberate
                // off unless the receiver says so.
                if (f.flags & EE_FLAG_ANOMALY) {
                    Serial.println(F("  ANOMALOUS — restore point lost"));
                } else {
                    Serial.println();
                }
            } else {
                rejected++;
                Serial.print(F("[RX] ee rejected: crc got="));
                Serial.print(f.crc, HEX);
                Serial.print(F(" exp="));
                Serial.println(frame_crc16(&f, sizeof(f)), HEX);
            }

        } else if (magic == NRF_MAGIC_PWM) {
            PwmFrame f;
            memcpy(&f, buf, sizeof(f));

            if (f.crc == frame_crc16(&f, sizeof(f))) {
                pwmFrames++;
                pwm_duty = f.duty;
                // Printed on every frame, not just on change: the sender repeats
                // the duty once per cycle so a late-booting receiver converges,
                // and that also makes this a live readout of the current value.
                Serial.print("[RX] PWM ");
                Serial.print(f.duty);
                Serial.print(" permille @ ");
                Serial.print(f.hz);
                Serial.print((f.flags & PWM_FLAG_ACTIVE)
                                 ? F(" Hz  (modulating)") : F(" Hz  (endpoint, held)"));
                // Where the duty came from. The transmitter never ages a duty
                // out, so this is the only thing distinguishing "being commanded
                // right now" from "holding whatever was persisted at boot" --
                // and a quiet node looks identical to a frozen one otherwise.
                Serial.print(F("  src="));
                Serial.print(pwm_src_str(f.flags));
                pwm_source = (uint8_t)(f.flags & PWM_SRC_MASK);
                pwm_src_have = 1u;
                Serial.println();
            } else {
                rejected++;
                Serial.print("[RX] pwm rejected: crc got=");
                Serial.print(f.crc, HEX);
                Serial.print(" exp=");
                Serial.println(frame_crc16(&f, sizeof(f)), HEX);
            }

        } else {
            rejected++;
            Serial.print("[RX] unknown magic ");
            Serial.println(magic, HEX);
        }
    }

    serial_command();

    /* Retry a radio that did not come up, rather than leaving the bench tool
     * permanently deaf after a marginal boot. */
    if (!radioReady && (long)(millis() - radioRetryMs) >= 0) {
        radioRetryMs = millis() + 5000UL;
        if (radio_bring_up()) {
            radioReady = true;
            Serial.println(F("[RX] radio up"));
        }
    }
    if (!radioReady) {
        Serial.println(F("[RX] nRF24 still absent — no packets possible"));
        return;
    }

    if (replyPending && (long)(millis() - replyAtMs) >= 0) {
        replyPending = false;
        send_pwm_command(pwm_target);
    }

    /* Idle line: printed only when genuinely nothing is arriving, and it says
     * what is missing rather than counting things that happened long ago. */
    if (!gotAny && (unsigned long)(millis() - lastStatus) >= 3000u) {
        unsigned long quiet = lastFrameMs ? (unsigned long)(millis() - lastFrameMs) : 0u;
        uint8_t k, n = 0;

        lastStatus = millis();
        for (k = 0u; k < NRF_MAX_SENSORS; k++) n += known[k] ? 1u : 0u;

        Serial.print(F("[RX] "));
        if (lastFrameMs == 0u) {
            Serial.print(F("NO FRAMES EVER"));
        } else {
            Serial.print(F("quiet "));
            Serial.print((uint32_t)(quiet / 1000u));
            Serial.print(F("s, last was "));
            Serial.print(lastKind < RXK_COUNT ? seenName[lastKind] : "?");
        }

        Serial.print(F("  learned "));
        Serial.print(n);
        Serial.print("/");
        Serial.print(NRF_MAX_SENSORS);

        /* Named absences are the actionable part. A cumulative count of zero is
         * not: it cannot be told apart from a type that simply is not enabled on
         * the transmitter, whereas "never seen" can be read against what the
         * transmitter is supposed to be sending. */
        {
            uint8_t missing = 0u;
            for (k = 0u; k < RXK_COUNT; k++) {
                if (seenAt[k] == 0u) {
                    if (missing == 0u) Serial.print(F("  never saw:"));
                    Serial.print(F(" "));
                    Serial.print(seenName[k]);
                    missing = 1u;
                }
            }
            if (missing == 0u) {
                /* All types seen at some point — now report how stale the
                 * non-critical ones are, since that is what actually goes wrong
                 * in the field.
                 *
                 * The threshold is PER TYPE, because the node does not send
                 * everything at one rate: the ADC and PWM frames go out about
                 * once a second, temperatures and bus stats every 5 s, but
                 * identity and EEPROM status are deliberately slow at 60 s
                 * because neither changes quickly. One flat cutoff flagged a
                 * perfectly healthy 60 s-cadence frame as stale half the time,
                 * which trains you to ignore the one word on the line that is
                 * supposed to mean something. Each budget below is the node's own
                 * period plus generous slack for a long loop pass. */
                for (k = 0u; k < RXK_COUNT; k++) {
                    unsigned long age = (unsigned long)(millis() - seenAt[k]);
                    if (age > staleBudgetMs[k]) {
                        Serial.print(F("  stale "));
                        Serial.print(seenName[k]);
                        Serial.print("=");
                        Serial.print((uint32_t)(age / 1000u));
                        Serial.print(F("s"));
                    }
                }
            }
        }

        if (ee_have) {
            Serial.print(F("  eeprom:"));
            Serial.print(ee_restored);
            if (ee_flags & EE_FLAG_ANOMALY) {
                Serial.print(F(" RESTORE-LOST"));
            } else if (ee_flags & EE_FLAG_EMPTY) {
                Serial.print(F(" EMPTY"));
            } else {
                Serial.print(F(" rec="));
                Serial.print(ee_records);
                /* Ring geometry, which the frame has always carried and this line
                 * never showed. The arc and the write count answer the two
                 * questions that matter when a restore looks wrong: how much of
                 * the ring is in use, and whether anything is being written at
                 * all. A node restoring something old with writes=0 and a
                 * small count is not touching its log, which is the thing to
                 * know before suspecting the restore walk. */
                Serial.print(F("/"));
                Serial.print((uint16_t)256u);
                Serial.print(F(" arc="));
                Serial.print(ee_oldest);
                Serial.print(F(".."));
                Serial.print(ee_newest);
                Serial.print(F(" w="));
                Serial.print(ee_writes);
            }
            if (ee_flags & EE_FLAG_PWMFAIL) {
                /* The flag is durable and the ring holds 80 hours, so it can mean
                 * "failing right now" or "failed once last week". Nothing in the
                 * frame carries an age — there is no clock on either side — so the
                 * only honest answer available is how long THIS receiver has been
                 * seeing it. That at least separates ongoing from historical, and
                 * starts from zero if the flag is cleared while we are watching. */
                if (pwmfailSinceMs == 0u) pwmfailSinceMs = millis();
                Serial.print(F(" PWM-OUTPUT-FAILED "));
                Serial.print((uint32_t)((unsigned long)(millis() - pwmfailSinceMs) / 1000u));
                Serial.print(F("s"));
            } else {
                pwmfailSinceMs = 0u;   /* cleared, or not yet seen */
            }
        }

        if (stat_have) {
            if (!stat_present) {
                Serial.print(F("  [NO CAN on transmitter]"));
            } else if (stat_eflg) {
                /* Only when non-zero. EFLG is read-to-clear on the transmitter,
                 * so a clear report afterwards does not erase the fact that it
                 * happened — which is why a fault seen once here is worth keeping
                 * on the idle line rather than only in its own frame. */
                Serial.print(F("  bus-eflg=0x"));
                Serial.print(stat_eflg, HEX);
                if (stat_eflg & CAN_EFLAG_TXBO) Serial.print(F(" TX-BUSOFF"));
                if (stat_eflg & CAN_EFLAG_TXEP) Serial.print(F(" TX-ERRPASSIVE"));
                if (stat_eflg & CAN_EFLAG_RX0OVR || stat_eflg & CAN_EFLAG_RX1OVR)
                    Serial.print(F(" RX-OVERRUN"));
            }
        }

        if (pwm_src_have) {
            Serial.print(F("  duty="));
            Serial.print(pwm_duty);
            Serial.print(F(" src="));
            Serial.print(pwm_src_str(pwm_source));
            if (pwm_source == PWM_SRC_EEPROM) Serial.print(F("(restored)"));
        }

        /* Rejected frames are an integrity problem, not a liveness one, so they
         * only earn a place on the line when non-zero. */
        if (rejected) {
            Serial.print(F("  REJECTED="));
            Serial.print(rejected);
        }
        Serial.println();
    }

    /* Typed-but-unsubmitted digits. Printed every pass so a half-typed value is
     * visible, and the only thing here that is not about the link. */
    if (serialLen) {
        Serial.print(F("[RX] kbd "));
        for (uint8_t i = 0u; i < serialLen; i++) Serial.print(serialBuf[i]);
        Serial.println(F(" ... (Enter to send)"));
    }
}