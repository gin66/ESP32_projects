/**
 * @file haus_heater.h
 * @brief Heater control modes, scheduled flow law, and authority management
 *        for the 28_hauszentrale panel.
 *
 * Defines the four authority states the heating system can be in:
 *
 *   OFF       — heating is completely off (duty 1000 on the bus).
 *   MANUAL    — a user is directly controlling it (slider/dial on the detail
 *               page). The value travels on the bus until the user changes it
 *               or the system takes over.
 *   AUTO      — the panel decides automatically from schedules and sensors.
 *               The panel may transmit duty 100..500 or 1000 (real off) on the
 *               bus; the value is subject to anti-short-cycling rules.
 *   OVERRIDE  — a foreign writer (nRF or another node) is temporarily in charge.
 *               The panel yields for a bounded period (2 h), then returns to
 *               AUTO.
 *
 * The scheduled flow law (§5.3) computes the duty from a day/night schedule:
 *   day:  T_flow = T_day  (default 55 °C → duty 283 on the adopted table)
 *   night: T_flow = T_day − 15 K, floored at 42 °C → duty 500
 *   off:   duty = 1000  (manual OFF, schedule-off, or economy lockout)
 *   frost: T_flow = a modest point on the adopted table, independent of the
 *          schedule
 *
 * Authority management (§5.1, §5.2, 0.4):
 *   - Manual OFF while AUTO → MODE_OFF, send 1000 immediately, persist,
 *     cancel AUTO.
 *   - Deliberate manual duty while AUTO → MODE_MANUAL, send user value once,
 *     persist.
 *   - AUTO control → enter MODE_AUTO only when CAN is green and inputs are
 *     valid.
 *   - Foreign 0x280 / nRF change → MODE_OVERRIDE for bounded time (2 h),
 *     then return to AUTO.
 *   - Reboot → restore mode from /heater.bin; read 0x280; send only when it
 *     disagrees.
 *   - Sensor loss → hold, then AUTO HOLD; do not silently switch modes.
 *   - Unsynced clock → hold; do not apply day flow until CanState.synced.
 *   - Schedule/sensor recovery cannot override MODE_OFF.
 *
 * See §5.1 and §5.3 of whitepaper_auto_heating.md.
 */

#ifndef HAUS_HEATER_H
#define HAUS_HEATER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  HEATER_MODE_OFF = 0,    /**< Heating off (duty 1000, no schedule applied). */
  HEATER_MODE_MANUAL,     /**< User directly controls the duty value.       */
  HEATER_MODE_AUTO,       /**< Panel decides from schedule + sensors.      */
  HEATER_MODE_OVERRIDE,   /**< Foreign writer is temporarily in charge.    */
} HeaterMode;

/* ─── Scheduled flow law (v1, §5.3) ──────────────────────────────────────── */

/* A day/night schedule that produces a flow temperature (Soll) for the
 * heating system. Temperatures are in c° (°C × 100).
 *
 * The schedule is evaluated using local wall-clock time. The firmware TZ is
 * `CET-1CEST,M3.5.0/2:00,M10.5.0/3:00`; the schedule times are local.
 *
 * During the spring gap (clock jumps forward), the schedule holds the current
 * state until the next stable minute. During the fall-back hour, the schedule
 * holds the current state until the next stable minute.
 *
 * Default values produce 55 °C day flow (duty 283) and 40 °C night flow
 * (floored at 42 °C → duty 500). */
typedef struct {
  uint8_t day_start_hour;   /**< Hour when day flow begins (0–23). Default: 6.  */
  uint8_t night_start_hour; /**< Hour when night setback begins (0–23). Default: 22. */
  int16_t T_day_c100;       /**< Day flow temperature in c°. Default: 5500 (55 °C). */
  int16_t night_setback_c100; /**< Night setback in c°. Default: 1500 (15 K). */
  int16_t night_floor_c100; /**< Floor for night flow. Default: 4200 (42 °C). */
} HeaterSchedule;

/* Initialize the schedule with defaults: 55 °C day, 15 K setback, 42 °C floor,
 * 06:00–22:00 day period. */
void heater_schedule_default(HeaterSchedule *schedule);

/* Evaluate the scheduled flow law (v1, §5.3).
 *
 * Parameters:
 *   schedule    — the day/night schedule (non-NULL).
 *   utc_epoch   — current time as unix epoch seconds (0 = unsynced).
 *   clock_synced — true when the clock is synced via SNTP.
 *   mode        — current heater mode.
 *   frost_active — true when frost demand is active.
 *   out_duty    — receives the computed duty (non-NULL).
 *   out_reason  — receives a reason code (non-NULL).
 *
 * Returns:
 *   true  — duty was computed and written.
 *   false — the schedule cannot produce a duty (e.g. unsynced clock in AUTO).
 *
 * Reason codes (HeaterScheduleReason):
 *   HEATER_SCHED_DAY          — day flow, normal operation.
 *   HEATER_SCHED_NIGHT        — night flow (T_day − setback, floored).
 *   HEATER_SCHED_OFF          — schedule-off (not heating time).
 *   HEATER_SCHED_FROST        — frost demand overrides schedule.
 *   HEATER_SCHED_UNSYNCED     — clock unsynced, hold restored duty.
 *   HEATER_SCHED_MODE_OFF     — mode is OFF, schedule not applied.
 *
 * Rules:
 *   - Day flow: T_flow = T_day → duty via the adopted inverse table.
 *   - Night flow: T_flow = T_day − setback, floored at night_floor → duty.
 *   - Real off: duty = 1000 (manual OFF, schedule-off, or economy lockout).
 *   - Frost demand: modest point on the adopted table, independent of the
 *     schedule. Frost demand is always active when frost_active is true,
 *     regardless of day/night.
 *   - Manual OFF beats frost protection (mode OFF → duty 1000).
 *   - Until the clock is synced (clock_synced == false, utc_epoch == 0),
 *     hold the restored duty. Do not apply the day flow. Returns false with
 *     reason HEATER_SCHED_UNSYNCED.
 *   - The schedule edge means "heating starts", not "room is warm".
 *   - A fixed preheat lead (60–90 min) is deferred until the house time
 *     constant is known.
 */
typedef enum {
  HEATER_SCHED_DAY = 0,       /**< Day flow, normal operation.          */
  HEATER_SCHED_NIGHT,         /**< Night flow (T_day − setback).       */
  HEATER_SCHED_OFF,           /**< Schedule-off (not heating time).    */
  HEATER_SCHED_FROST,         /**< Frost demand overrides schedule.     */
  HEATER_SCHED_UNSYNCED,      /**< Clock unsynced, hold restored duty.  */
  HEATER_SCHED_MODE_OFF,      /**< Mode is OFF, schedule not applied.   */
} HeaterScheduleReason;

bool heater_schedule_compute(const HeaterSchedule *schedule,
                              uint32_t utc_epoch,
                              bool clock_synced,
                              HeaterMode mode,
                              bool frost_active,
                              uint16_t *out_duty,
                              HeaterScheduleReason *out_reason);

/* ─── Authority management (0.4) ─────────────────────────────────────────── */

/* Reason codes for AUTO HOLD state. */
typedef enum {
  HEATER_HOLD_NONE = 0,          /**< No hold — normal operation.       */
  HEATER_HOLD_CAN_NOT_GREEN,     /**< CAN bus not healthy.              */
  HEATER_HOLD_SENSORS_INVALID,   /**< Required sensor inputs missing.   */
  HEATER_HOLD_CLOCK_UNSYNCED,    /**< RTC clock not synced via SNTP.    */
  HEATER_HOLD_MODE_OFF_LOCKED,   /**< MODE_OFF active, recovery blocked. */
  HEATER_HOLD_SENSOR_STALE,      /**< Sensor data too old.              */
  HEATER_HOLD_SCHED_OFF,         /**< Schedule says off (not heating).  */
} HeaterHoldReason;

/* Authority state for the heater control system.
 *
 * This struct encapsulates the authority/boot/foreign-writer handling (0.4):
 *
 *   - mode: the current authority mode (OFF, MANUAL, AUTO, OVERRIDE).
 *   - saved_duty: the duty value to transmit when in MANUAL mode (100..500).
 *   - override_start_epoch: monotonic seconds when OVERRIDE began (0 = none).
 *   - override_max_seconds: maximum OVERRIDE duration (default 7200 = 2 h).
 *   - can_green: true when the CAN bus is healthy and communicative.
 *   - sensors_valid: true when all required sensor inputs are valid.
 *   - clock_synced: true when the RTC clock is synced via SNTP.
 *   - mode_off_locked: true when MODE_OFF is active and cannot be overridden
 *     by schedule/sensor recovery.
 *   - last_transmitted_duty: the last duty value transmitted on the bus
 *     (used for boot comparison).
 *   - hold_reason: the reason for AUTO HOLD (if in AUTO but cannot transmit).
 */
typedef struct {
  HeaterMode mode;                       /**< Current authority mode.          */
  uint16_t saved_duty;                   /**< Saved duty for MANUAL mode.      */
  uint32_t override_start_epoch;         /**< Monotonic seconds of OVERRIDE.   */
  uint32_t override_max_seconds;         /**< Max OVERRIDE duration (default 7200). */
  bool can_green;                        /**< CAN bus healthy.                 */
  bool sensors_valid;                    /**< All sensor inputs valid.         */
  bool clock_synced;                     /**< RTC clock synced via SNTP.       */
  bool mode_off_locked;                  /**< MODE_OFF cannot be overridden.   */
  uint16_t last_transmitted_duty;        /**< Last duty sent on bus.           */
  HeaterHoldReason hold_reason;          /**< Reason for AUTO HOLD.            */
  uint32_t eeprom_last_write_epoch;      /**< Monotonic seconds of last EEPROM write (0 = never). */
} HeaterAuthority;

/* Initialize authority state with defaults. */
void heater_authority_default(HeaterAuthority *auth);

/* Handle manual OFF while in AUTO or MANUAL.
 *
 * Parameters:
 *   auth       — authority state (non-NULL, updated).
 *   current_duty_on_bus — the current duty value reported on the CAN bus
 *                        (from 0x280).
 *
 * Returns:
 *   true  — a transmit is needed (send 1000).
 *   false — no transmit needed (bus already shows 1000 or mode was already OFF).
 *
 * Behavior:
 *   - Switches to MODE_OFF.
 *   - Sets mode_off_locked = true (schedule/sensor recovery cannot override).
 *   - If current_duty_on_bus == 1000, no transmit needed (return false).
 *   - Otherwise, transmit 1000 once (return true).
 *   - Clears saved_duty (OFF has no saved duty).
 */
bool heater_handle_manual_off(HeaterAuthority *auth, uint16_t current_duty_on_bus);

/* Handle manual duty takeover while in AUTO.
 *
 * Parameters:
 *   auth          — authority state (non-NULL, updated).
 *   user_duty     — the duty value the user selected (100..500).
 *   current_duty_on_bus — the current duty value on the CAN bus (0x280).
 *
 * Returns:
 *   true  — a transmit is needed (send user_duty once).
 *   false — no transmit needed (bus already shows user_duty).
 *
 * Behavior:
 *   - Switches to MODE_MANUAL.
 *   - Sets mode_off_locked = false.
 *   - Saves user_duty for future reference.
 *   - If current_duty_on_bus == user_duty, no transmit (return false).
 *   - Otherwise, transmit user_duty once (return true).
 *   - Cancels any active OVERRIDE.
 */
bool heater_handle_manual_duty(HeaterAuthority *auth,
                                uint16_t user_duty,
                                uint16_t current_duty_on_bus);

/* Attempt to enter AUTO mode.
 *
 * Parameters:
 *   auth       — authority state (non-NULL, updated).
 *   computed_duty — the duty computed by the schedule law (100..500 or 1000).
 *
 * Returns:
 *   true  — AUTO entered, duty is valid for transmission.
 *   false — AUTO cannot be entered (preconditions not met, hold active).
 *
 * Preconditions for AUTO entry:
 *   - can_green == true (CAN bus healthy).
 *   - sensors_valid == true (all required sensor inputs available).
 *   - clock_synced == true (RTC time synced).
 *   - mode_off_locked == false (not locked in OFF).
 *
 * If any precondition fails:
 *   - Stay in current mode (do not silently switch).
 *   - Set hold_reason to the first unmet condition.
 *   - Return false.
 *
 * If all preconditions met:
 *   - Switch to MODE_AUTO.
 *   - Clear hold_reason.
 *   - Return true with the computed_duty.
 */
bool heater_try_enter_auto(HeaterAuthority *auth, uint16_t computed_duty);

/* Handle foreign writer (nRF or 0x280 from another node).
 *
 * Parameters:
 *   auth            — authority state (non-NULL, updated).
 *   monotonic_now  — current monotonic time in seconds.
 *   duty_from_bus   — the duty value received from the foreign source.
 *
 * Returns:
 *   true  — mode changed to OVERRIDE (yield to foreign writer).
 *   false — mode was already OVERRIDE and still within bounds, or
 *           the foreign duty matches what we would send.
 *
 * Behavior:
 *   - Switches to MODE_OVERRIDE.
 *   - Records override_start_epoch = monotonic_now.
 *   - Saves current mode to allow return after expiry.
 *   - Does not transmit — yield to foreign writer.
 *   - If already in OVERRIDE and within bounds, no change (return false).
 */
bool heater_handle_foreign_writer(HeaterAuthority *auth,
                                   uint32_t monotonic_now,
                                   uint16_t duty_from_bus);

/* Check if OVERRIDE has expired and return to AUTO.
 *
 * Parameters:
 *   auth       — authority state (non-NULL, updated).
 *   monotonic_now — current monotonic time in seconds.
 *
 * Returns:
 *   true  — OVERRIDE expired, mode changed (to AUTO if preconditions met,
 *           otherwise hold).
 *   false — OVERRIDE still active, or already returned to AUTO.
 *
 * Behavior:
 *   - If mode == OVERRIDE and
 *     (monotonic_now - override_start_epoch) >= override_max_seconds:
 *       * OVERRIDE expired.
 *       * Try to enter AUTO (check preconditions).
 *       * If preconditions met → MODE_AUTO, return true.
 *       * If not → stay in current mode with hold, return true.
 *   - Otherwise, no change (return false).
 */
bool heater_check_override_expiry(HeaterAuthority *auth,
                                   uint32_t monotonic_now);

/* Handle sensor loss / stale sensor data.
 *
 * Parameters:
 *   auth    — authority state (non-NULL, updated).
 *   stale   — true when required sensor data is stale/missing.
 *
 * Returns:
 *   true  — hold was activated or reason changed.
 *   false — no change (sensor was already valid, or not in AUTO).
 *
 * Behavior:
 *   - If stale == true and mode == AUTO:
 *       * Set hold_reason = HEATER_HOLD_SENSOR_STALE.
 *       * Do NOT change mode (hold current mode).
 *       * Return true.
 *   - If stale == false and mode == AUTO and hold_reason == HEATER_HOLD_SENSOR_STALE:
 *       * Clear hold_reason (sensors recovered).
 *       * If preconditions now met, can re-enter AUTO.
 *       * Return true.
 *   - If not in AUTO, no change (return false).
 */
bool heater_handle_sensor_loss(HeaterAuthority *auth, bool stale);

/* Handle clock unsynced state.
 *
 * Parameters:
 *   auth       — authority state (non-NULL, updated).
 *   synced     — true when the clock is now synced.
 *
 * Returns:
 *   true  — hold was activated or cleared.
 *   false — no change (clock was already in the same state).
 *
 * Behavior:
 *   - If synced == false and mode == AUTO:
 *       * Set hold_reason = HEATER_HOLD_CLOCK_UNSYNCED.
 *       * Do NOT change mode (hold current mode).
 *       * Return true.
 *   - If synced == true and mode == AUTO and hold_reason == HEATER_HOLD_CLOCK_UNSYNCED:
 *       * Clear hold_reason (clock recovered).
 *       * Return true.
 *   - If not in AUTO, no change (return false).
 */
bool heater_handle_clock_unsynced(HeaterAuthority *auth, bool synced);

/* Handle CAN bus state change.
 *
 * Parameters:
 *   auth    — authority state (non-NULL, updated).
 *   green   — true when the CAN bus is healthy.
 *
 * Returns:
 *   true  — hold was activated or cleared.
 *   false — no change (CAN was already in the same state).
 *
 * Behavior:
 *   - If green == false and mode == AUTO:
 *       * Set hold_reason = HEATER_HOLD_CAN_NOT_GREEN.
 *       * Do NOT change mode (hold current mode).
 *       * Return true.
 *   - If green == true and mode == AUTO and hold_reason == HEATER_HOLD_CAN_NOT_GREEN:
 *       * Clear hold_reason (CAN recovered).
 *       * Return true.
 *   - If not in AUTO, no change (return false).
 */
bool heater_handle_can_state(HeaterAuthority *auth, bool green);

/* Get the duty to transmit (if any) given the current authority state.
 *
 * Parameters:
 *   auth         — authority state (non-NULL).
 *   computed_duty — the duty from the schedule (if in AUTO).
 *   out_duty     — receives the duty to transmit (non-NULL).
 *   out_send     — receives whether to transmit (non-NULL).
 *   out_src      — receives the source of the duty (non-NULL, may be NULL).
 *
 * Returns:
 *   true  — a duty was determined and written.
 *   false — no duty applicable (e.g., no schedule computed).
 *
 * Source codes (HeaterTxSource):
 *   HEATER_TX_NONE   — no transmit needed.
 *   HEATER_TX_MANUAL — duty from manual user input.
 *   HEATER_TX_AUTO   — duty from the schedule.
 *   HEATER_TX_EEPROM — duty from restored EEPROM (boot).
 *   HEATER_TX_NRF    — duty from nRF/foreign writer.
 */
typedef enum {
  HEATER_TX_NONE = 0,     /**< No transmit.                         */
  HEATER_TX_MANUAL,       /**< From manual user input.              */
  HEATER_TX_AUTO,         /**< From schedule (AUTO mode).           */
  HEATER_TX_EEPROM,       /**< From restored EEPROM (boot state).   */
  HEATER_TX_NRF,          /**< From nRF/foreign writer.             */
} HeaterTxSource;

bool heater_get_transmit_duty(const HeaterAuthority *auth,
                               uint16_t computed_duty,
                               uint16_t current_duty_on_bus,
                               uint16_t *out_duty,
                               bool *out_send,
                               HeaterTxSource *out_src);

/* Handle reboot: restore mode and determine boot transmit.
 *
 * Parameters:
 *   auth              — authority state (non-NULL, updated from persisted data).
 *   persisted_mode    — the mode restored from /heater.bin.
 *   persisted_duty    — the duty restored from /heater.bin (for MANUAL mode).
 *   current_duty_on_bus — the duty currently on the CAN bus (0x280).
 *   can_green         — current CAN bus health.
 *   sensors_valid     — current sensor validity.
 *   clock_synced      — current clock sync status.
 *   out_duty          — receives the duty to transmit (non-NULL).
 *   out_send          — receives whether to transmit (non-NULL).
 *   out_src           — receives the source (non-NULL).
 *
 * Returns:
 *   true  — a transmit decision was made.
 *   false — no transmit needed (bus already matches restored state).
 *
 * Rules:
 *   - OFF + report is 1000: send nothing.
 *   - OFF + report is heating duty: send 1000 once.
 *   - MANUAL: send saved_duty only when report differs.
 *   - AUTO: send nothing until first legal decision; show PWM_SRC_EEPROM.
 *   - Handle PWM_SRC_NRF and foreign 0x280 → yield, don't overwrite remote.
 */
bool heater_handle_boot_restore(HeaterAuthority *auth,
                                 HeaterMode persisted_mode,
                                 uint16_t persisted_duty,
                                 uint16_t current_duty_on_bus,
                                 bool can_green,
                                 bool sensors_valid,
                                 bool clock_synced,
                                 uint16_t *out_duty,
                                 bool *out_send,
                                 HeaterTxSource *out_src);

/* Check if schedule/sensor recovery is blocked by MODE_OFF.
 *
 * Parameters:
 *   auth   — authority state (non-NULL).
 *
 * Returns:
 *   true  — MODE_OFF is locked, recovery cannot override.
 *   false — MODE_OFF is not locked, recovery may proceed.
 */
bool heater_is_mode_off_locked(const HeaterAuthority *auth);

/* Get the current hold reason (for UI display). */
HeaterHoldReason heater_get_hold_reason(const HeaterAuthority *auth);

#ifdef __cplusplus
}
#endif

#endif /* HAUS_HEATER_H */
