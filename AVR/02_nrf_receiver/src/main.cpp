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

// Consume serial input until Enter. Accepts only decimal digits so there is no
// pull-in from random characters, and range-checks before accepting.
static void serial_command()
{
    while (Serial.available()) {
        char c = (char)Serial.read();

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

    radio.stopListening();
    bool ok = radio.write(&f, sizeof(f));
    radio.startListening();

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

// ─── Setup ───────────────────────────────────────────────────────────────────
void setup()
{
    Serial.begin(115200);
    Serial.println("[RX] boot");

    if (!radio.begin()) {
        Serial.println("[RX] ERROR: nRF24 not responding!");
        while (1) {}
    }

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

    Serial.println("[RX] ready — broadcasts on pipe 1, commands on pipe 0");
    Serial.println("[RX] type a duty 0..1000 and press Enter to command the PWM");
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
    static uint16_t pwm_duty = 0xFFFF;   // no PWM frame seen yet

    // Last CAN stats received, so the idle summary can show current bus health
    // rather than nothing at all between the 5 s reports.
    static bool     stat_have = false;
    static uint16_t stat_sent = 0, stat_recv = 0, stat_refused = 0;
    static uint8_t  stat_eflg = 0;
    static bool     stat_present = false;   // does the transmitter have a controller

    bool gotAny = false;

    while (radio.available()) {
        gotAny = true;

        uint8_t buf[NRF_FRAME_SIZE];
        radio.read(buf, sizeof(buf));

        uint16_t magic;
        memcpy(&magic, buf, sizeof(magic));

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
                stat_sent = f.sent;
                stat_recv = f.received;
                stat_refused = f.refused;
                stat_eflg = f.eflg;
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
                Serial.println((f.flags & PWM_FLAG_ACTIVE)
                                 ? F(" Hz  (modulating)") : F(" Hz  (endpoint, held)"));
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

    if (replyPending && (long)(millis() - replyAtMs) >= 0) {
        replyPending = false;
        send_pwm_command(pwm_target);
    }

    if (!gotAny && millis() - lastStatus >= 5000) {
        lastStatus = millis();
        uint8_t n = 0;
        for (uint8_t i = 0; i < NRF_MAX_SENSORS; i++) n += known[i] ? 1 : 0;

        Serial.print("[RX] waiting — learned ");
        Serial.print(n);
        Serial.print("/");
        Serial.print(NRF_MAX_SENSORS);
        Serial.print("  temps=");
        Serial.print(tempFrames);
        Serial.print("  ids=");
        Serial.print(idFrames);
        Serial.print("  pwm=");
        Serial.print(pwmFrames);
        Serial.print("  adc=");
        Serial.print(adcFrames);
        Serial.print("  stats=");
        Serial.print(statsFrames);
        if (stat_have) {
            // Repeats the live health bits: a fault that has since cleared still
            // shows here, because the last report is all this node has.
            if (!stat_present) {
                // Absent beats every other reading, so it goes first and short.
                Serial.print(F("  [NO CAN]"));
            } else {
            Serial.print("  [eflg=0x");
            Serial.print(stat_eflg, HEX);
            if (stat_eflg) Serial.print(F("!"));
            Serial.print(F(" sent="));
            Serial.print(stat_sent);
            Serial.print(F(" refused="));
            Serial.print(stat_refused);
            Serial.print("]");
            }
        }
        Serial.print("  rejected=");
        Serial.print(rejected);
        Serial.print("  duty=");
        if (pwm_duty == 0xFFFF) {
            Serial.print("--");           // no PWM frame seen yet
        } else {
            Serial.print(pwm_duty);
        }
        Serial.print(" permille  kbd=");
        Serial.println((uint16_t)serialLen);   // digits buffered, awaiting Enter
    }
}