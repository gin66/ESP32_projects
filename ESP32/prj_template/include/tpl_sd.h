/**
 * @file tpl_sd.h
 * @brief SD card module for ESP32 template
 *
 * Provides SPI-based SD card initialization and HTTP endpoints for
 * listing, reading, downloading, status, and re-initialization.
 *
 * Configuration (build flags in platformio.ini):
 * - TPL_SD_CS: SD chip-select pin (default 5)
 * - TPL_SD_MOSI: SD MOSI pin (default 23)
 * - TPL_SD_MISO: SD MISO pin (default 19)
 * - TPL_SD_CLK: SD clock pin (default 18)
 * - TPL_SD_SPI_FREQ: SPI frequency in Hz (default 4000000)
 * - NO_TPL_SD: Define to disable SD card functionality
 */

#ifdef USE_TPL_SD

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

extern bool tpl_sd_initialized;
extern char tpl_sd_status[64];

bool tpl_sd_setup();
void tpl_sd_register_endpoints();
bool tpl_sd_reinit();
String tpl_sd_list_dir(const char* dirname, uint8_t levels);

#endif
