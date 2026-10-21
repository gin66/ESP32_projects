/**
 * @file tpl_sd.h
 * @brief SD card module for ESP32 template
 *
 * Provides SPI-based SD card initialization and the /sd HTTP endpoints
 * (list, read, download, delete, status, re-initialization).
 *
 * By default the card is mounted on VSPI with a GPIO chip-select configured by
 * the TPL_SD_* build flags. A board whose CS is not a GPIO (for example an I2C
 * expander bit) passes a TplSdBackend to tpl_sd_setup(); the backend owns the
 * SPI pin mapping and the CS mechanism.
 *
 * Configuration (build flags in platformio.ini):
 * - TPL_SD_CS: SD chip-select pin (default 5)
 * - TPL_SD_MOSI: SD MOSI pin (default 23)
 * - TPL_SD_MISO: SD MISO pin (default 19)
 * - TPL_SD_CLK: SD clock pin (default 18)
 * - TPL_SD_SPI_FREQ: SPI frequency in Hz (default 4000000)
 * - USE_TPL_SD: Define to enable SD card functionality
 */

#ifdef USE_TPL_SD

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

/**
 * Board-specific SD bus and chip-select implementation.
 *
 * begin() must configure the SPI pins and assert an external CS if needed.
 * It runs before SD.begin(); end() runs on unmount and must release the bus
 * and any external CS.
 */
struct TplSdBackend {
  SPIClass* spi;
  int ss_gpio;
  bool (*begin)();
  void (*end)();
};

extern bool tpl_sd_initialized;
extern char tpl_sd_status[64];

/**
 * Mount the card. Pass NULL to use the built-in GPIO-CS backend.
 */
bool tpl_sd_setup(const TplSdBackend* backend = nullptr);

/**
 * Register the /sd HTTP endpoints. Set include_status to false when the
 * project registers its own richer /sd/status handler, and include_reinit to
 * false when a project-specific mount owns remounting.
 */
void tpl_sd_register_endpoints(bool include_status = true,
                               bool include_reinit = true);

bool tpl_sd_reinit();

/** Unmount. Returns false and leaves the mount state untouched if the lock
 * cannot be taken. */
bool tpl_sd_end();

/**
 * Serialize card access. The web handlers take this lock; project code that
 * touches SD directly (recorder, probes) should take it too.
 */
bool tpl_sd_lock(uint32_t timeout_ms);
void tpl_sd_unlock();

String tpl_sd_list_dir(const char* dirname, uint8_t levels);

#endif
