/**
 * @file tpl_websocket.h
 * @brief WebSocket server module for ESP32 template
 *
 * Provides a WebSocket server on port 81 for real-time communication:
 * - Periodic status broadcasts (GPIO states, memory, CPU load)
 * - Remote GPIO control via JSON commands
 * - Camera image streaming (on ESP32-CAM)
 *
 * JSON Commands supported:
 * - pin_to_high, pin_to_low: Set GPIO state
 * - as_input, as_input_pullup, as_input_pulldown: Set GPIO mode
 * - sleep, deepsleep, save_config: System control
 * - image, flash: Camera control (ESP32-CAM only)
 *
 * Security: GPIO commands validate pin numbers to prevent undefined behavior.
 */

#include <ArduinoJson.h>  // already included in UniversalTelegramBot.h
#include <WebSockets.h>
#include <WebSocketsClient.h>
#include <WebSocketsServer.h>

/// Global WebSocket server instance on port 81
extern WebSocketsServer webSocket;

/**
 * @brief Initialize and start the WebSocket server
 *
 * @param publish Callback to add custom data to status broadcasts
 * @param process Callback to handle custom JSON commands
 *
 * Creates a FreeRTOS task on Core 0 to handle WebSocket communication.
 * Broadcasts system status every 750ms when clients are connected.
 */
void tpl_websocket_setup(void (*publish)(DynamicJsonDocument* json),
                         void (*process)(DynamicJsonDocument* json));
