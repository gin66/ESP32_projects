/**
 * @file haus_sd.cpp
 * @brief Project-specific SD-card mount for the Waveshare ESP32-S3 board.
 *
 * The board's SD_CS is CH422G IO4 (active LOW). The Arduino SD library needs a
 * GPIO for CS, so a spare GPIO is passed as a dummy and the expander bit is
 * asserted for the whole mount. The SD SPI bus uses the board pins
 * MOSI 11 / SCLK 12 / MISO 13.
 *
 * This is deliberately separate from tpl_sd: tpl_sd drives a real GPIO CS and
 * cannot mount this board's card (whitepaper §8.15.4).
 */

#include "haus_sd.h"

#include "tpl_ws7.h"
#include "tpl_debug_log.h"

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <string.h>

#define HAUS_SD_DUMMY_CS 15
#define HAUS_SD_FREQ 4000000

static SPIClass s_sd_spi(HSPI);
static bool s_mounted = false;

bool haus_sd_mount(void) {
    if (s_mounted && haus_sd_is_mounted()) return true;
    if (s_mounted) haus_sd_unmount();

    /* CH422G IO4 must be push-pull, then asserted (active LOW). */
    if (tpl_ws7_sd_cs_enable_oc_pp() != ESP_OK) {
        TPL_LOGE("sd", "CS push-pull setup failed");
        return false;
    }
    if (tpl_ws7_sd_select(true) != ESP_OK) {
        TPL_LOGE("sd", "CS assert failed");
        return false;
    }

    /* The SD library toggles the dummy GPIO; the expander CS stays low, so the
     * card is selected for the whole session. Only one device is on this bus. */
    s_sd_spi.begin(TPL_WS7_SD_SCLK, TPL_WS7_SD_MISO, TPL_WS7_SD_MOSI,
                   HAUS_SD_DUMMY_CS);
    if (!SD.begin(HAUS_SD_DUMMY_CS, s_sd_spi, HAUS_SD_FREQ, "/sd", 5, false)) {
        TPL_LOGE("sd", "mount failed (card absent or not responding)");
        tpl_ws7_sd_select(false);
        return false;
    }

    uint8_t type = SD.cardType();
    if (type == CARD_NONE) {
        TPL_LOGE("sd", "mount succeeded but no card");
        SD.end();
        tpl_ws7_sd_select(false);
        return false;
    }

    const char *type_str = "UNKNOWN";
    if (type == CARD_MMC) type_str = "MMC";
    else if (type == CARD_SD) type_str = "SDSC";
    else if (type == CARD_SDHC) type_str = "SDHC";

    TPL_LOGI("sd", "mounted: %s, %llu MB total, %llu MB used\n",
             type_str, SD.totalBytes() / (1024ULL * 1024ULL),
             SD.usedBytes() / (1024ULL * 1024ULL));

    s_mounted = true;
    return true;
}

void haus_sd_unmount(void) {
    if (s_mounted) {
        SD.end();
        s_sd_spi.end();
        s_mounted = false;
    }
    tpl_ws7_sd_select(false);
}

bool haus_sd_is_mounted(void) {
    if (!s_mounted) return false;
    /* cardType() may be cached; totalBytes() performs a real FAT access, so a
     * pulled card is detected here rather than at the next write. */
    if (SD.cardType() == CARD_NONE) return false;
    if (SD.totalBytes() == 0) return false;
    return true;
}

bool haus_sd_selftest(void) {
    if (!s_mounted) return false;

    const char *path = "/sd_test.txt";
    const char *payload = "hauszentrale sd bring-up\n";

    File f = SD.open(path, FILE_WRITE);
    if (!f) {
        TPL_LOGE("sd", "self-test: open for write failed");
        return false;
    }
    const size_t wrote = f.print(payload);
    f.close();
    if (wrote != strlen(payload)) {
        TPL_LOGE("sd", "self-test: short write");
        return false;
    }

    f = SD.open(path, FILE_READ);
    if (!f) {
        TPL_LOGE("sd", "self-test: open for read failed");
        return false;
    }
    String back = f.readString();
    f.close();
    SD.remove(path);

    if (back != payload) {
        TPL_LOGE("sd", "self-test: read-back mismatch (%u bytes)\n",
                 (unsigned)back.length());
        return false;
    }

    TPL_LOGI("sd", "self-test: write/read/delete OK");
    return true;
}
