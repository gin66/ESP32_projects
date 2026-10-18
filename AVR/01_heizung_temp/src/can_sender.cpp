#include <Arduino.h>
#include <SPI.h>
#include "can_sender.h"
#include "serial_log.h"
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

// TXBnCTRL, and the bits that describe why a transmission stalled. The library
// exposes none of these — readRegister()/modifyRegister() are private — so the
// same raw SPI used by can_spi_probe() below is used here rather than patching
// a dependency.
#define REG_TXB0CTRL      0x30
#define REG_TXB1CTRL      0x40
#define REG_TXB2CTRL      0x50
#define TXB_ABTF          0x40
#define TXB_MLOA          0x20
#define TXB_TXERR         0x10
#define TXB_TXREQ         0x08
#define TXB_FAULT_MASK    (TXB_TXREQ | TXB_ABTF | TXB_MLOA | TXB_TXERR)

// Acceptance-filter registers. The library's register enum is private, same
// reason as the TXBnCTRL addresses above.
#define REG_RXF0SIDH      0x00
#define REG_RXF5SIDH      0x18
#define REG_RXM0SIDH      0x20
#define REG_RXM1SIDH      0x24
#define REG_RXB0CTRL      0x60
#define REG_RXB1CTRL      0x70
// RXBnCTRL.RXM = 01: standard identifiers that meet the filter, only.
#define RXB_RXM_STD       0x20
#define RXB_RXM_MASK      0x60
// RXB0 rolls a second accepted frame into RXB1 instead of setting RX0OVR.
#define RXB0_BUKT         0x04

static uint8_t spi_read_reg(uint8_t addr)
{
    uint8_t v;
    SPI.beginTransaction(SPISettings(MCP_SPI_CLOCK, MSBFIRST, SPI_MODE0));
    digitalWrite(MCP2515_CS_PIN, LOW);
    SPI.transfer(MCP_INSTR_READ);
    SPI.transfer(addr);
    v = SPI.transfer(0x00);
    digitalWrite(MCP2515_CS_PIN, HIGH);
    SPI.endTransaction();
    return v;
}

static void spi_write_reg(uint8_t addr, uint8_t val)
{
    SPI.beginTransaction(SPISettings(MCP_SPI_CLOCK, MSBFIRST, SPI_MODE0));
    digitalWrite(MCP2515_CS_PIN, LOW);
    SPI.transfer(MCP_INSTR_WRITE);
    SPI.transfer(addr);
    SPI.transfer(val);
    digitalWrite(MCP2515_CS_PIN, HIGH);
    SPI.endTransaction();
}

// Automatic bus-off recovery.
//
// The MCP2515 latches bus-off in EFLG.TXBO and the only way out on this board
// is the SPI RESET instruction — no ~RESET pin is broken out. Left off, one
// latched fault would take CAN down until the next power cycle, so it is on.
// The "off while the fault is being isolated" behaviour is a diagnostic
// choice, not a safety one: this node is not the bus owner and has no reason to
// sit out a fault it is not causing.
#define CAN_AUTO_RECOVER     1

// How long a transmit buffer may sit with TXREQ set before it is aborted.
//
// Normal arbitration on a 125 kbit/s bus resolves in well under a millisecond
// and an ACK arrives within one frame, so this is roughly two orders of
// magnitude above anything legitimate. It exists for the one case that
// otherwise wedges the node permanently: nothing on the bus ACKs, the
// controller retries the same frame forever, all three TX buffers fill with
// TXREQ set, and every later send returns ERROR_ALLTXBUSY. See tx_abort_stuck().
#define TX_STUCK_MS          250UL

// Backoff between bus-off recovery attempts. A reset is not free — it drops
// every queued frame — so this must not become a retry loop of its own.
#define RECOVER_BACKOFF_MS   2000UL

void can_csn_high()
{
    digitalWrite(MCP2515_CS_PIN, HIGH);
    pinMode(MCP2515_CS_PIN, OUTPUT);
}

static MCP2515 mcp(MCP2515_CS_PIN, MCP_SPI_CLOCK);

static uint16_t sent = 0;
static uint16_t received = 0;
static uint16_t dropped = 0;
static uint16_t rxMalformed = 0;
static uint16_t rxOverflows = 0;
static uint16_t spiErrors = 0;
static bool      ready = false;

// Stuck-TX accounting. txAborts is the headline number: any non-zero value
// means frames were being discarded because nothing acknowledged them.
static uint32_t txAborts = 0;
static uint8_t  txAbortFaults = 0xFF;   // TXBnCTRL of the last buffer aborted
static bool     txAbortReportLeft = true;  // report the first one, then be quiet

static bool          pwmPending = false;
static uint16_t      pwmDuty = 0;

static bool          busOff = false;
static uint8_t       busOffEflg = 0;
static uint16_t      recoveries = 0;
static uint16_t      recoverTries = 0;
static unsigned long lastRecoverMs = 0;

// ─── Stuck transmit-buffer detection ─────────────────────────────────────────
// sendMessage() hands a frame to a TX buffer and returns; it never waits for an
// ACK. With the controller's auto-retry enabled, a frame that is never
// acknowledged simply stays there with TXREQ set, indefinitely. Three such
// frames occupy all three buffers and every subsequent send returns
// ERROR_ALLTXBUSY — so the node goes permanently mute, and nothing in the
// firmware ever clears it. This is not hypothetical: it is what happens
// whenever the other end of the bus is off for a few seconds.
//
// The repair is the one the controller provides for exactly this condition —
// writing 0 to TXREQ aborts the pending transmission and returns the buffer to
// the available pool. It costs one register read and, only when a buffer is
// actually stuck, one register write.
static unsigned long txPendingSince[3] = { 0, 0, 0 };
static bool         txPending[3]       = { false, false, false };

static void tx_abort_stuck()
{
    static const uint8_t ctrl[3] = { REG_TXB0CTRL, REG_TXB1CTRL, REG_TXB2CTRL };

    unsigned long now = millis();

    for (uint8_t i = 0; i < 3; i++) {
        uint8_t v = spi_read_reg(ctrl[i]);

        if (v == 0xFF) {          // no controller answering on MISO
            spiErrors++;
            txPending[i] = false;
            continue;
        }

        if (!(v & TXB_TXREQ)) {
            txPending[i] = false;
            continue;
        }

        if (!txPending[i]) {      // first sighting: start the clock
            txPending[i] = true;
            txPendingSince[i] = now;
            continue;
        }

        if ((unsigned long)(now - txPendingSince[i]) < TX_STUCK_MS) continue;

        // Abort the transmission. The fault bits go with it so the next send
        // starts from a clean buffer: TXREQ alone does not reliably clear
        // ABTF/MLOA/TXERR, and a stale MLOA would misdescribe the next stall.
        spi_write_reg(ctrl[i], (uint8_t)(v & ~TXB_FAULT_MASK));
        txPending[i] = false;
        txAborts++;
        txAbortFaults = v;

        if (txAbortReportLeft) {
            txAbortReportLeft = false;
            log_print(F("[CAN] aborted stuck TX buffer "));
            log_print(i);
            log_print(F(" ctrl=0x"));
            log_println(v, HEX);
        }
    }
}

// Drop every identifier except the PWM command before it can occupy a buffer.
//
// rx_drain() only acts on CAN_ID_PWM_SET. reset() leaves both masks at zero,
// which is "don't care" for every ID bit, so the high-rate frames are loaded
// into the two RX buffers and thrown away in software. Two buffers is the
// whole queue: a burst sets RX0OVR/RX1OVR and the 0x240 that arrived in that
// window is the frame that was lost. The same burst also changes CANINTF
// between the three reads in readIntfStable(), so rx_drain() bails and the
// buffers stay full.
//
// RXB0 is gated by RXF0/RXF1 against RXM0, RXB1 by RXF2–RXF5 against RXM1.
// One mask left at zero accepts every ID into that buffer, so both masks
// compare all 11 standard-ID bits and all six filters hold CAN_ID_PWM_SET.
// The filters are standard frames (EXIDE clear) and RXBnCTRL.RXM is
// standard-only, so an extended frame matches nothing.
//
// BUKT stays set. A second 0x240 then rolls into RXB1 while the first is
// still unread. Rollover ignores RXB1's own filters, which is safe only
// because RXB0 itself accepts just this one ID.
//
// setFilter() leaves the controller in config mode; the caller enters normal
// or listen-only afterwards. This lives in configure() rather than only in
// can_init() because a bus-off recovery resets the chip, and that reset
// restores the accept-everything masks.
static bool accept_only_pwm()
{
    if (mcp.setFilterMask(MCP2515::MASK0, false, CAN_SFF_MASK) != MCP2515::ERROR_OK)
        return false;
    if (mcp.setFilterMask(MCP2515::MASK1, false, CAN_SFF_MASK) != MCP2515::ERROR_OK)
        return false;

    const MCP2515::RXF order[] = {
        MCP2515::RXF0, MCP2515::RXF1, MCP2515::RXF2,
        MCP2515::RXF3, MCP2515::RXF4, MCP2515::RXF5
    };
    for (uint8_t i = 0; i < 6; i++) {
        if (mcp.setFilter(order[i], false, CAN_ID_PWM_SET) != MCP2515::ERROR_OK)
            return false;
    }

    spi_write_reg(REG_RXB0CTRL, (uint8_t)(RXB_RXM_STD | RXB0_BUKT));
    spi_write_reg(REG_RXB1CTRL, RXB_RXM_STD);

    // SIDH is ID[10:3]. A mask of 0x7FF stores 0xFF there. BUKT1 (RXB0CTRL
    // bit 1) is a read-only copy of BUKT, so the compare ignores it.
    const uint8_t sidh = (uint8_t)(CAN_ID_PWM_SET >> 3);
    const uint8_t rxm_bukt = (uint8_t)(RXB_RXM_MASK | RXB0_BUKT);
    bool ok = spi_read_reg(REG_RXM0SIDH) == 0xFF
           && spi_read_reg(REG_RXM1SIDH) == 0xFF
           && spi_read_reg(REG_RXF0SIDH) == sidh
           && spi_read_reg(REG_RXF5SIDH) == sidh
           && (spi_read_reg(REG_RXB0CTRL) & rxm_bukt) == (uint8_t)(RXB_RXM_STD | RXB0_BUKT)
           && (spi_read_reg(REG_RXB1CTRL) & RXB_RXM_MASK) == RXB_RXM_STD;
    if (!ok) {
        static bool reported = false;
        if (!reported) {
            reported = true;
            log_println(F("[CAN] acceptance filter did not read back"));
        }
        return false;
    }
    return true;
}

static bool configure()
{
    if (mcp.reset() != MCP2515::ERROR_OK) return false;
    if (mcp.setBitrate(CAN_125KBPS, MCP_8MHZ) != MCP2515::ERROR_OK) return false;
    // Before the mode switch. A filter call drops the chip back to config
    // mode and leaves it there; entering normal first would take the node
    // off the bus again, and a failed filter must not leave it accepting
    // every ID.
    if (!accept_only_pwm()) return false;

#if CAN_LISTEN_ONLY
    if (mcp.setListenOnlyMode() != MCP2515::ERROR_OK) return false;
#else
    if (mcp.setNormalMode() != MCP2515::ERROR_OK) return false;
#endif

    // A freshly reset controller is not in bus-off and has no error counters,
    // whatever the previous incarnation was doing. Clearing here means both
    // can_init() and can_recover() converge on the same clean state, and the
    // pump cannot immediately re-trigger a recovery that has already happened.
    busOff = false;
    busOffEflg = 0;
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
        log_println(F("[CAN] dump: no controller"));
        return;
    }

    uint8_t eflg = mcp.getErrorFlags();
    log_print(F("[CAN] library 8 MHz / 125 kbit/s  mode="));
#if CAN_LISTEN_ONLY
    log_print(F("listen-only"));
#else
    log_print(F("normal"));
#endif
    log_print(F("  accept=0x"));
    log_print(CAN_ID_PWM_SET, HEX);
    log_print(F("  EFLG=0x"));
    log_print(eflg, HEX);
    log_print(F(" INTF=0x"));
    log_print(mcp.getInterrupts(), HEX);
    log_print(F(" TEC="));
    log_print(mcp.errorCountTX());
    log_print(F(" REC="));
    log_println(mcp.errorCountRX());

    uint8_t got[PROBE_LEN];
    uint8_t bad = can_spi_probe(got);
    log_print(F("[CAN] spi probe mismatch="));
    log_print(bad);
    if (bad != 0) {
        log_print(F(" read="));
        for (uint8_t i = 0; i < PROBE_LEN; i++) {
            log_print(F(" "));
            if (got[i] < 0x10) log_print('0');
            log_print(got[i], HEX);
        }
    }
    log_println();
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

    // Never write the pattern into a buffer that is still transmitting. The
    // datasheet is explicit that TXBnSIDH/EID8/D0-D7 must not be written while
    // TXREQ is set, and a buffer stuck with TXREQ set is exactly the state
    // tx_abort_stuck() exists to clear — so probing here would corrupt the very
    // frame the abort is about to discard, and inject a garbage extended-ID
    // frame onto the bus besides. Report "busy" instead and let the caller
    // count it as a non-answer; there is nothing to learn from a probe that
    // cannot be run safely.
    if (spi_read_reg(REG_TXB0CTRL) & TXB_TXREQ) {
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
        // The acceptance filter already admits only CAN_ID_PWM_SET. The
        // compare stays so a frame that should not have been loaded — an
        // RTR, or a filter that did not stick — cannot become a duty.
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
    // Bus-off recovery.
    //
    // The MCP2515 latches bus-off in EFLG.TXBO and cannot leave it without the
    // SPI RESET instruction — no ~RESET pin is broken out on this board — so
    // without this path CAN stays dead until the next power cycle. The attempt
    // is backed off: the reset drops every queued frame, and an un-backed-off
    // retry loop would be an outage of its own making.
    //
    // Nothing else can work while the controller is off the bus, so the rest of
    // the pump is skipped until recovery has been tried.
    if (busOff) {
        if ((unsigned long)(millis() - lastRecoverMs) >= RECOVER_BACKOFF_MS) {
            lastRecoverMs = millis() + RECOVER_BACKOFF_MS;
            recoverTries++;

            uint8_t tec = 0, rec = 0;
            can_error_counters(&tec, &rec);
            log_print(F("[CAN] bus-off  eflg=0x"));
            log_print(busOffEflg, HEX);
            log_print(F(" tec="));
            log_print(tec);
            log_print(F(" rec="));
            log_print(rec);
            log_print(F(" try="));
            log_print(recoverTries);

            // can_recover() clears busOff on success; if it fails it leaves
            // ready false, and the peripheral state machine in main.cpp then
            // falls back to plain can_init() retries.
            if (can_recover()) {
                log_println(F(" — recovered"));
            } else {
                log_println(F(" — reset did not take, will retry"));
            }
        }
        return;
    }

    if (!ready) return;

    // Before rx_drain(), so a buffer that is wedged from a previous fault is
    // returned to the pool before this pass tries to transmit on top of it.
    tx_abort_stuck();
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

bool can_submit_low(uint16_t id, const uint8_t data[8])
{
    struct can_frame f;

    if (!ready) return false;
    if (spi_read_reg(REG_TXB2CTRL) & TXB_TXREQ) return false;

    f.can_id  = id & CAN_SFF_MASK;
    f.can_dlc = 8;
    memcpy(f.data, data, 8);

    if (mcp.sendMessage(MCP2515::TXB2, &f) != MCP2515::ERROR_OK) return false;
    sent++;
    return true;
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

bool can_send_ee(const haus_can_ee_t* e)
{
    uint8_t d[8] = { 0 };
    haus_can_set_ee(d, e);
    return submit(CAN_ID_EE_REPORT, d);
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

uint16_t can_recover_tries()
{
    return recoverTries;
}

uint32_t can_tx_aborts()
{
    return txAborts;
}

uint8_t can_tx_abort_faults()
{
    return txAbortFaults;
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
