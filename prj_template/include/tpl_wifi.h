#include <Arduino.h>
#include <driver/gpio.h>

/**
 * @file tpl_wifi.h
 * @brief WiFi and OTA update module for ESP32 template
 *
 * Provides:
 * - WiFi connection management with automatic reconnection
 * - Multi-AP support (connects to strongest known network)
 * - mDNS responder for hostname.local discovery
 * - ArduinoOTA for over-the-air firmware updates
 * - NTP time synchronization
 *
 * Network credentials are defined in wifi_secrets.cpp (symlinked from
 * .private/)
 *
 * @note OTA authentication is disabled by default. Enable in code for
 * production.
 */

/**
 * @brief Initialize WiFi, OTA, and network services
 *
 * @param verbose Enable detailed serial output
 * @param waitOTA Wait up to 10 seconds for OTA updates before proceeding
 * @param ledPin GPIO pin for status LED during OTA wait (255 to disable)
 *
 * Creates a FreeRTOS task on Core 0 for WiFi management.
 * Blocks until WiFi is connected and time is synchronized.
 */
void tpl_wifi_setup(bool verbose, bool waitOTA = true,
                    gpio_num_t ledPin = (gpio_num_t)255);

typedef void (*tpl_wifi_reconnect_callback_t)(unsigned long current_ms);

void tpl_wifi_register_reconnect(tpl_wifi_reconnect_callback_t callback);
