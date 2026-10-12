#include <Arduino.h>
#include <SPI.h>
#include "can_sender.h"
#include "haus_can.h"

#include "mcp2515.h"
#include "can.h"

// rf-nano V1.0: RF24 already owns CE=D10 / CSN=D9, 1-Wire is on D4.
// The MCP2515 shares the hardware SPI bus with the nRF24. Only the chip
// select differs. Every transfer goes through autowp/autowp-mcp2515.
#define MCP2515_CS_PIN    8    // MCP2515 CSN (D8)
#define MCP2515_INT_PIN   2    // MCP2515 INT  (D2)

// 1 MHz. The library default is 10 MHz, which this wiring reads as 0xFF.
#define MCP_SPI_CLOCK     1000000
#define MCP_INSTR_WRITE   0x02
#define MCP_INSTR_READ    0x03
#define REG_TXB0SIDH      0x31
#define PROBE_LEN         13

// Automatic bus-off recovery. Off while the fault is being isolated: a reset
// clears TEC/REC. can_recover() is still available.
#define CAN_AUTO_RECOVER     0

void can_csn_high()
{
    digitalWrite(MCP2515_CS_PIN, HIGH);
    pinMode(MCP2515_CS_PIN, OUTPUT);
}

static MCP2515 mcp(MCP2515_CS_PIN, MCP_SPI_CLOCK);

static uint16_t  sent = 0;
static uint16_t  received = 0;
static uint16_t  dropped = 0;
static uint16_t  rxMalformed = 0;
static uint16_t  rxOverflows = 0;
static uint16_t  spiErrors = 0;
static bool      ready = false;

static bool          pwmPending = false;
static uint16_t      pwmDuty = 0;

static bool          busOff = false;
static bool          busOffReported = false;
static uint8_t       busOffEflg = 0;
static uint16_t      recoveries = 0;
#if CAN_AUTO_RECOVER
static unsigned long lastRecoverMs = 0;
#endif

static bool configure()
{
    if (mcp.reset() != MCP2515::ERROR_OK) return false;
    if (mcp.setBitrate(CAN_125KBPS, MCP_8MHZ) != MCP2515::ERROR_OK) return false;

#if CAN_LISTEN_ONLY
    if (mcp.setListenOnlyMode() != MCP2515::ERROR_OK) return false;
#else
    if (mcp.setNormalMode() != MCP2515::ERROR_OK) return false;
#endif
    return true;
}

bool can_init()
{
    if (ready) return true;
    ready = configure();
    return ready;
}

bool can_recover()
{
    ready = configure();
    if (ready) {
        recoveries++;
        busOff = false;
    }
    return ready;
}

void can_error_counters(uint8_t* tec, uint8_t* rec)
{
    if (tec != nullptr) *tec = ready ? mcp.errorCountTX() : 0;
    if (rec != nullptr) *rec = ready ? mcp.errorCountRX() : 0;
}

void can_dump_config()
{
    if (!ready) {
        Serial.println(F("[CAN] dump: no controller"));
        return;
    }

    uint8_t eflg = mcp.getErrorFlags();
    Serial.print(F("[CAN] library 8 MHz / 125 kbit/s  mode="));
#if CAN_LISTEN_ONLY
    Serial.print(F("listen-only"));
#else
    Serial.print(F("normal"));
#endif
    Serial.print(F("  EFLG=0x"));
    Serial.print(eflg, HEX);
    Serial.print(F(" INTF=0x"));
    Serial.print(mcp.getInterrupts(), HEX);
    Serial.print(F(" TEC="));
    Serial.print(mcp.errorCountTX());
    Serial.print(F(" REC="));
    Serial.println(mcp.errorCountRX());

    uint8_t got[PROBE_LEN];
    uint8_t bad = can_spi_probe(got);
    Serial.print(F("[CAN] spi probe mismatch="));
    Serial.print(bad);
    if (bad != 0) {
        Serial.print(F(" read="));
        for (uint8_t i = 0; i < PROBE_LEN; i++) {
            Serial.print(F(" "));
            if (got[i] < 0x10) Serial.print('0');
            Serial.print(got[i], HEX);
        }
    }
    Serial.println();
}

// Write 13 bytes at TXB0SIDH (not the control byte, so TXREQ stays clear)
// and read them back. Returns how many bytes differ. 0 means the SPI write
// and the SPI read agree. got may be null.
uint8_t can_spi_probe(uint8_t* got)
{
    // Byte 1 is TXB0SIDL. Bits 4 and 2 are unimplemented and read back as 0,
    // so 0x5A comes back as 0x4A on a good SPI link. The pattern keeps those
    // bits clear. Byte 4 is DLC; bits 7, 5 and 4 are unimplemented too.
    static const uint8_t pat[PROBE_LEN] = {
        0xA5, 0x4A, 0x3C, 0xC3, 0x0F, 0xF0, 0x81,
        0x7E, 0x12, 0x34, 0x56, 0x78, 0x9A
    };
    uint8_t buf[PROBE_LEN];
    if (!ready) {
        if (got != nullptr) memset(got, 0, PROBE_LEN);
        return 0xFF;
    }

    SPI.beginTransaction(SPISettings(MCP_SPI_CLOCK, MSBFIRST, SPI_MODE0));
    digitalWrite(MCP2515_CS_PIN, LOW);
    SPI.transfer(MCP_INSTR_WRITE);
    SPI.transfer(REG_TXB0SIDH);
    for (uint8_t i = 0; i < PROBE_LEN; i++) SPI.transfer(pat[i]);
    digitalWrite(MCP2515_CS_PIN, HIGH);
    SPI.endTransaction();

    SPI.beginTransaction(SPISettings(MCP_SPI_CLOCK, MSBFIRST, SPI_MODE0));
    digitalWrite(MCP2515_CS_PIN, LOW);
    SPI.transfer(MCP_INSTR_READ);
    SPI.transfer(REG_TXB0SIDH);
    for (uint8_t i = 0; i < PROBE_LEN; i++) buf[i] = SPI.transfer(0x00);
    digitalWrite(MCP2515_CS_PIN, HIGH);
    SPI.endTransaction();

    uint8_t bad = 0;
    for (uint8_t i = 0; i < PROBE_LEN; i++) {
        if (buf[i] != pat[i]) bad++;
    }
    if (got != nullptr) memcpy(got, buf, PROBE_LEN);
    return bad;
}

// A real CANINTF value holds still. Three identical reads are required before
// a mailbox is touched, and three identical reads without that flag are
// required before recv moves. One stray 0x00 looked like "flag cleared" and
// counted the same mailbox on every loop pass.
static bool readIntfStable(uint8_t* out)
{
    uint8_t a = mcp.getInterrupts();
    uint8_t b = mcp.getInterrupts();
    uint8_t c = mcp.getInterrupts();
    if (a == 0xFF || b == 0xFF || c == 0xFF) {
        spiErrors++;
        return false;
    }
    if (a != b || b != c) return false;
    *out = a;
    return true;
}

static void rx_drain()
{
    static bool badLatched = false;

    for (uint8_t n = 0; n < 2; n++) {
        uint8_t intf = 0;
        if (!readIntfStable(&intf)) return;

        uint8_t pending = intf & (MCP2515::CANINTF_RX0IF | MCP2515::CANINTF_RX1IF);
        if (pending == 0) return;

        bool rx0 = pending & MCP2515::CANINTF_RX0IF;
        uint8_t flag = rx0 ? MCP2515::CANINTF_RX0IF : MCP2515::CANINTF_RX1IF;
        MCP2515::RXBn which = rx0 ? MCP2515::RXB0 : MCP2515::RXB1;

        struct can_frame f;
        MCP2515::ERROR err = mcp.readMessage(which, &f);

        uint8_t after = 0;
        if (!readIntfStable(&after)) return;
        if ((after & flag) || err != MCP2515::ERROR_OK) {
            if (err != MCP2515::ERROR_OK && !badLatched) {
                rxMalformed++;
                badLatched = true;
            }
            return;
        }
        badLatched = false;

        received++;
        if ((f.can_id & (CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_ERR_FLAG)) == 0
            && (uint16_t)f.can_id == CAN_ID_PWM_SET
            && f.can_dlc >= 2) {
            uint16_t duty;
            memcpy(&duty, f.data, sizeof(duty));
            if (duty > 1000) duty = 1000;
            pwmDuty = duty;
            pwmPending = true;
        }
    }
}

void can_pump()
{
    if (busOff && !busOffReported) {
        busOffReported = true;

        uint8_t tec = 0, rec = 0;
        can_error_counters(&tec, &rec);
        Serial.print(F("[CAN] bus-off  eflg=0x"));
        Serial.print(busOffEflg, HEX);
        Serial.print(F(" tec="));
        Serial.print(tec);
        Serial.print(F(" rec="));
        Serial.print(rec);
#if CAN_AUTO_RECOVER
        Serial.println(F(" — resetting MCP2515"));
        can_recover();
#else
        Serial.println(F(" (auto-recover disabled)"));
#endif
    }
    if (!busOff) busOffReported = false;

    if (!ready) return;
    rx_drain();
}

bool can_ready()
{
    return ready;
}

static bool submit(uint16_t id, const uint8_t data[8])
{
    if (!ready) return false;

    struct can_frame f;
    f.can_id = id & CAN_SFF_MASK;
    f.can_dlc = 8;
    memcpy(f.data, data, 8);

    if (mcp.sendMessage(&f) != MCP2515::ERROR_OK) {
        dropped++;
        return false;
    }

    sent++;
    return true;
}

bool can_send_identity(uint8_t rank, const uint8_t rom[8])
{
    if (rank >= NRF_MAX_SENSORS) return false;
    uint8_t d[8] = { 0 };
    haus_can_identity_t id;
    memcpy(id.rom, rom, 8);
    haus_can_set_identity(d, &id);
    return submit((uint16_t)(CAN_ID_ID_BASE + rank), d);
}

bool can_send_temperatures(const int16_t temps[NRF_MAX_SENSORS])
{
    uint8_t d[8] = { 0 };
    haus_can_temperature_t t;
    memcpy(t.temperature, temps, sizeof(t.temperature));
    haus_can_set_temperature(d, &t);
    return submit(CAN_ID_DATA_BASE, d);
}

bool can_send_adc(uint16_t max_count, uint16_t min_count, uint16_t pp, uint8_t flags)
{
    uint8_t d[8] = { 0 };
    haus_can_adc_t a;
    a.max_count = max_count;
    a.min_count = min_count;
    a.pp_count  = pp;
    a.flags     = flags;
    haus_can_set_adc(d, &a);
    return submit(CAN_ID_ADC_REPORT, d);
}

bool can_send_pwm(uint16_t duty_permille, uint16_t hz, uint8_t flags)
{
    uint8_t d[8] = { 0 };
    haus_can_pwm_t p;
    p.duty_permille = duty_permille;
    p.frequency_hz  = hz;
    p.flags         = flags;
    haus_can_set_pwm(d, &p);
    return submit(CAN_ID_PWM_REPORT, d);
}

bool can_send_stats(uint16_t s, uint16_t r, uint16_t refused,
                    uint8_t eflg, uint8_t canstat, uint8_t present)
{
    uint8_t d[8] = { 0 };
    haus_can_stats_t st;
    st.sent      = s;
    st.received  = r;
    st.refused   = refused;
    st.eflg      = eflg;
    st.canstat   = canstat;
    st.flags     = present ? STATS_FLAG_PRESENT : 0x00;
    haus_can_set_stats(d, &st);
    return submit(CAN_ID_STATS_REPORT, d);
}

bool can_read_pwm_command(uint16_t* duty_permille)
{
    if (!ready || duty_permille == nullptr || !pwmPending) return false;
    pwmPending = false;
    if (pwmDuty == *duty_permille) return false;
    *duty_permille = pwmDuty;
    return true;
}

uint16_t can_rx_malformed()
{
    return rxMalformed;
}

uint16_t can_rx_overflows()
{
    return rxOverflows;
}

uint16_t can_recoveries()
{
    return recoveries;
}

uint16_t can_spi_errors()
{
    return spiErrors;
}

uint16_t can_dropped()
{
    return dropped;
}

void can_stats(uint16_t* s, uint16_t* r, uint16_t* refused,
               uint8_t* eflg, uint8_t* tec, uint8_t* rec, uint8_t* intf)
{
    if (s != nullptr)       *s       = sent;
    if (r != nullptr)       *r       = received;
    if (refused != nullptr) *refused = dropped;

    if (!ready) {
        if (eflg != nullptr) *eflg = 0;
        if (tec != nullptr)  *tec  = 0;
        if (rec != nullptr)  *rec  = 0;
        if (intf != nullptr) *intf = 0;
        return;
    }

    uint8_t flags = mcp.getErrorFlags();
    if (flags == 0xFF) {
        spiErrors++;
    } else {
        if (flags & (CAN_EFLAG_RX0OVR | CAN_EFLAG_RX1OVR)) {
            rxOverflows++;
            mcp.clearRXnOVRFlags();
        }
        if (flags & CAN_EFLAG_TXBO) {
            busOff = true;
            busOffEflg = flags;
        }
    }
    if (eflg != nullptr) *eflg = flags;
    if (tec != nullptr)  *tec  = mcp.errorCountTX();
    if (rec != nullptr)  *rec  = mcp.errorCountRX();
    if (intf != nullptr) *intf = mcp.getInterrupts();
}

bool can_present()
{
    return ready;
}
