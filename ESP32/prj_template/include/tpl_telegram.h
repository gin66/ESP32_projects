/**
 * @file tpl_telegram.h
 * @brief Telegram bot integration for ESP32 template
 *
 * Provides Telegram bot functionality for remote control and notifications.
 *
 * Built-in commands:
 * - /start: Show help message
 * - /test: Show typing indicator, then reply
 * - /deepsleep: Trigger deep sleep mode
 * - /nosleep: Prevent deep sleep
 * - /allowsleep: Allow deep sleep
 * - /flash: Toggle camera flash (ESP32-CAM only)
 * - /image: Capture and send camera image (ESP32-CAM only)
 *
 * Configuration:
 * - BOTtoken: Define as token variable name from private_bot.h
 * - TELEGRAM_CERTIFICATE_ROOT: Root CA certificate for api.telegram.org
 */

#ifdef BOTtoken
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Initialize the Telegram bot
 *
 * @param chat_id Default chat ID for outgoing messages
 *
 * Creates a FreeRTOS task on Core 1 for bot message polling.
 * Checks for new messages every 1 second.
 *
 * @note Requires BOTtoken to be defined in build flags.
 */
void tpl_telegram_setup(const char* chat_id);
#endif
