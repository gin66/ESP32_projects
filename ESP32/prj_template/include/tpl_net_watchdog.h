/**
 * @file tpl_net_watchdog.h
 * @brief Network watchdog module for ESP32 template
 *
 * Monitors end-to-end network connectivity using NTP and DNS checks.
 * Coordinates with WiFi manager recovery tiers before triggering restart.
 *
 * Configuration:
 * - NET_WATCHDOG: Define as gateway IP (e.g., "192.168.1.1")
 * - NO_NET_WATCHDOG: Define to disable watchdog functionality
 *
 * Recovery tiers (managed by tpl_wifi):
 * - Tier 0: Normal operation
 * - Tier 1: DHCP reconnect (after 30s)
 * - Tier 2: WiFi stack reset (after 2min)
 * - Tier 3: ESP restart (after 5min, triggered here)
 */

#ifdef NET_WATCHDOG
#include <stdint.h>

/// Counter for consecutive network check failures
extern uint16_t tpl_fail;

/**
 * @brief Initialize and start the network watchdog
 *
 * @return true if task created successfully, false otherwise
 *
 * Creates a FreeRTOS task on Core 0 that checks network connectivity via NTP
 * (with DNS fallback) every second. Pauses during WiFi recovery (tier > 0).
 * Restarts the device after 5 minutes of failures when recovery exhausted.
 */
bool tpl_net_watchdog_setup();
#endif
