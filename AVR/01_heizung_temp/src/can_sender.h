/**
 * @file can_sender.h
 * @brief MCP2515 CAN output mirroring the nRF24 broadcast.
 *
 * Two classic CAN frames carry the same information the two nRF24 pipes carry:
 *
 *   id = CAN_ID_ID_BASE + rank, DLC 8 → the full 8-byte DS18B20 ROM
 *   id = CAN_ID_DATA_BASE,       DLC 8 → four centidegree readings
 *
 * No software CRC is added on any frame; the CAN controller already checks every
 * frame with hardware CRC-16 plus bit stuffing.
 *
 * The module is fail-soft: if no MCP2515 answers on the SPI bus, can_init()
 * returns false and every send becomes a no-op, so the temperature and nRF24
 * paths keep running on an unpopulated board.
 *
 * Receive is filtered in the MCP2515 to the one standard ID this node acts
 * on, CAN_ID_PWM_SET. The two RX buffers are not deep enough to hold the
 * rest of the bus.
 */

#ifndef CAN_SENDER_H
#define CAN_SENDER_H

#include <Arduino.h>
#include "haus_can.h"
#include "nrf_payload.h"

// Both translation units must see the same values: main.cpp gates the radio
// and the can_send_* calls, can_sender.cpp gates the MCP2515 mode.
// 1 = do not talk to the nRF24.
#define CAN_QUIET_RADIO 0
// 1 = listen-only: no transmit, only receive.
#define CAN_LISTEN_ONLY 0

// MCP2515 chip-select (D8) is active low. Output, held high, so the
// controller is not selected.
void can_csn_high();

// ─── PWM ─────────────────────────────────────────────────────────────────────
// One shared 8-byte payload layout for both PWM identifiers:
//   [0..1] duty in permille (uint16 little-endian)
//   [2..3] frequency in Hz
//   [4]    flags, PWM_FLAG_ACTIVE while modulating
//   [5..7] reserved

// Command: applied to the output verbatim, last writer wins. Returns true if a
// command frame was seen, in which case *duty_permille holds the new value.
bool can_read_pwm_command(uint16_t* duty_permille);

// Report: publish the duty the output is currently running.
bool can_send_pwm(uint16_t duty_permille, uint16_t hz, uint8_t flags);

// Report: one ADC measurement window, as raw counts.
//   [0..1] max, [2..3] min, [4..5] peak-to-peak, [6] flags, [7] reserved
// Counts are raw because the reference is internal — the sender cannot vouch for
// a bandgap's absolute accuracy, so no scaling is applied here.
bool can_send_adc(uint16_t max_count, uint16_t min_count, uint16_t pp, uint8_t flags);

// Report: bus health, so it can be watched over the radio rather than by having
// a scope on CAN. Fields are counters since boot plus the live register values.
//   [0..1] accepted for transmission
//   [2..3] received — frames read out of the RX buffers. The acceptance
//          filter admits only CAN_ID_PWM_SET, so this is not bus traffic.
//   [4..5] refused
//   [6]    EFLG, MCP2515 error flags, as read
//   [7]    CANSTAT, controller status, as read
bool can_send_stats(uint16_t sent, uint16_t received, uint16_t refused,
                    uint8_t eflg, uint8_t canstat, uint8_t present);

// 0x2C8 — restore-point log status. Separate from the stats frame because it
// changes on an 8 h cadence rather than a seconds one, and because it has to
// carry the restore *source*: a deliberate "off" and a lost restore point both
// produce duty = EPL_OFF_DUTY, and only the source tells them apart.
bool can_send_ee(const haus_can_ee_t* e);

// Whether a controller answered and came up. False means every send has been a
// no-op and all the counters stay at zero — indistinguishable from a healthy idle
// bus without this.
bool can_present();

// Configure the MCP2515 and place it in normal mode. False if no controller
// answered — the caller should report that and continue regardless.
bool can_init();

// Reset and reconfigure the controller. The MCP2515 latches bus-off (EFLG.TXBO)
// and cannot leave it without the SPI RESET instruction — which is the only way
// back on this board, since no ~RESET pin is broken out. This is what can_pump()
// invokes automatically, on a backoff, when bus-off is seen; callable by hand for
// tests. Returns false if the controller did not answer.
//
// A successful configure() clears the driver's bus-off state, so this also
// serves as the way to clear a fault that was never bus-off (a wedged SPI bus,
// say) without a power cycle.
bool can_recover();

// Number of successful automatic recoveries since boot.
uint16_t can_recoveries();

// Recovery attempts since boot, successful or not. A rising number with
// can_recoveries() flat means the controller is not answering the reset.
uint16_t can_recover_tries();

// Transmit buffers aborted since boot because they sat with TXREQ set for
// longer than a legitimate transmission can (see tx_abort_stuck()). Non-zero
// means frames were being discarded because nothing acknowledged them.
uint32_t can_tx_aborts();

// TXBnCTRL bits (ABTF/MLOA/TXERR/TXREQ) of the last buffer aborted, which say
// why the frame stalled: arbitration lost, message lost, or transmit error.
uint8_t can_tx_abort_faults();

// Times getErrorFlags() came back 0xFF (no reply on MISO).
uint16_t can_spi_errors();

// Live MCP2515 error counters from errorCountTX() / errorCountRX().
void can_error_counters(uint8_t* tec, uint8_t* rec);

// Print the library's error flags and counters once, after init.
void can_dump_config();

// Write a known 13-byte pattern into TX buffer 0 and read it back.
// Returns the number of bytes that differ. 0 means SPI write and read agree.
// If got is not null it receives the 13 bytes that were read.
uint8_t can_spi_probe(uint8_t* got);

// Consume received frames and count them. Must be called regularly: it is what
// empties the two receive buffers, and nothing else drains them. Only
// CAN_ID_PWM_SET is loaded into those buffers. It also does the two recovery
// jobs — aborting transmit buffers that are wedged with TXREQ
// set (which is what permanently mutes this node otherwise), and resetting a
// latched bus-off, which the MCP2515 cannot leave without a SPI RESET.
//
// There is no TX queue to advance. autowp's sendMessage() loads a buffer and
// returns, so a frame that was never ACKed is left to the controller's own retry
// logic rather than being tracked in software — see tx_abort_stuck() for how
// that is bounded.
void can_pump();

bool can_ready();

// true = accepted for transmission, false = no controller or queue saturated.
bool can_send_identity(uint8_t rank, const uint8_t rom[8]);
bool can_send_temperatures(const int16_t temps[NRF_MAX_SENSORS]);

// Number of frames the driver refused since boot (queue full, bad state, ...).
uint16_t can_dropped();

// Frames readMessage() rejected, and times EFLG showed a receive-buffer overrun.
uint16_t can_rx_malformed();
uint16_t can_rx_overflows();

// One sample from the library: software counters plus getErrorFlags(),
// errorCountTX/RX(), and getInterrupts(). No second read of the same register.
void can_stats(uint16_t* sent, uint16_t* received, uint16_t* refused,
               uint8_t* eflg, uint8_t* tec, uint8_t* rec, uint8_t* intf);

#endif // CAN_SENDER_H