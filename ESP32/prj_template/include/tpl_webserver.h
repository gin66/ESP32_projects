/**
 * @file tpl_webserver.h
 * @brief HTTP webserver module for ESP32 template
 *
 * Provides a webserver with endpoints for:
 * - GPIO control (/digitalwrite, /digital, /analog)
 * - System control (/restart, /deepsleep, /allowsleep, /nosleep)
 * - System status (/stack, /cpu, /watch)
 * - SPIFFS files (/spiffs/list, /spiffs/read, /spiffs/delete)
 *
 * tpl_fs_delete_allowed() is the shared check for POST /sd/delete and
 * POST /spiffs/delete. It rejects traversal and files the firmware keeps.
 * - OTA firmware updates (/update)
 *
 * Security: GPIO endpoints validate pin numbers to prevent undefined behavior.
 */

#include <WebServer.h>

/// Global webserver instance on port 80
extern WebServer tpl_server;

/**
 * @brief Initialize and start the HTTP webserver
 *
 * Creates a FreeRTOS task on Core 1 to handle client requests.
 * Sets up all standard endpoints and serves static files from SPIFFS.
 */
void tpl_webserver_setup();

/** True when a browser delete may remove this path on SD or SPIFFS. */
bool tpl_fs_delete_allowed(const char* path);
