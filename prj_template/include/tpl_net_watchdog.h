/**
 * @file tpl_net_watchdog.h
 * @brief Network watchdog module for ESP32 template
 *
 * Monitors network connectivity by pinging a gateway address.
 * If pings fail for 5 consecutive minutes, triggers a system restart.
 *
 * Configuration:
 * - NET_WATCHDOG: Define as gateway IP (e.g., "192.168.1.1")
 * - NO_NET_WATCHDOG: Define to disable watchdog functionality
 *
 * @note Uses ESP32Ping library for ICMP ping functionality.
 */

#ifdef NET_WATCHDOG
#include <stdint.h>

/// Counter for consecutive ping failures
extern uint16_t tpl_fail;

/**
 * @brief Initialize and start the network watchdog
 *
 * @return true if task created successfully, false otherwise
 *
 * Creates a FreeRTOS task on Core 0 that pings NET_WATCHDOG every second.
 * Restarts the device after 5 minutes of consecutive failures.
 */
bool tpl_net_watchdog_setup();
#endif
