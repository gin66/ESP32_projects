/**
 * @file template.h
 * @brief Main include file for ESP32 template system
 *
 * Include this file in your main.cpp to get access to all template modules.
 * Each module can be individually enabled/disabled via build flags.
 *
 * Available modules:
 * - tpl_system: Core system (required)
 * - tpl_wifi: WiFi and OTA (required for network features)
 * - tpl_webserver: HTTP server
 * - tpl_websocket: WebSocket server
 * - tpl_net_watchdog: Network health monitoring
 * - tpl_command: Command dispatch framework
 * - tpl_telegram: Telegram bot (requires BOTtoken)
 * - tpl_esp_camera: Camera support (requires IS_ESP32CAM)
 * - tpl_broadcast: UDP broadcast messaging
 * - tpl_matter: Matter protocol support (requires USE_TPL_MATTER)
 * - tpl_sd: SD card support (requires USE_TPL_SD)
 * - tpl_debug_log: Debug log ring buffer (requires USE_TPL_DEBUG_LOG)
 *
 * Typical usage:
 * @code
 * #include "template.h"
 *
 * void setup() {
 *   tpl_system_setup(0);  // 0 = no deep sleep
 *   tpl_wifi_setup(true, true, (gpio_num_t)tpl_ledPin);
 *   tpl_webserver_setup();
 *   tpl_websocket_setup(NULL, NULL);
 *   tpl_net_watchdog_setup();
 *   tpl_command_setup(NULL);
 * }
 *
 * void loop() {
 *   vTaskDelay(10 / portTICK_PERIOD_MS);
 * }
 * @endcode
 */

#include <stdint.h>

#include "tpl_broadcast.h"
#include "tpl_command.h"
#include "tpl_debug_log.h"
#include "tpl_esp_camera.h"
#include "tpl_matter.h"
#include "tpl_net_watchdog.h"
#include "tpl_sd.h"
#include "tpl_system.h"
#include "tpl_telegram.h"
#include "tpl_webserver.h"
#include "tpl_websocket.h"
#include "tpl_wifi.h"
