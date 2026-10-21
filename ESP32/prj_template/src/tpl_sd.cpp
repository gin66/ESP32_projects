#include "tpl_sd.h"
#include "tpl_debug_log.h"

#ifdef USE_TPL_SD

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "tpl_webserver.h"

#ifndef TPL_SD_CS
#define TPL_SD_CS 5
#endif
#ifndef TPL_SD_MOSI
#define TPL_SD_MOSI 23
#endif
#ifndef TPL_SD_MISO
#define TPL_SD_MISO 19
#endif
#ifndef TPL_SD_CLK
#define TPL_SD_CLK 18
#endif
#ifndef TPL_SD_SPI_FREQ
#define TPL_SD_SPI_FREQ 4000000
#endif
#ifndef TPL_SD_READ_MAX
#define TPL_SD_READ_MAX 65536
#endif

#if defined(VSPI)
static SPIClass s_default_spi(VSPI);
#else
static SPIClass s_default_spi(FSPI);
#endif
static bool s_bus_open = false;
static SemaphoreHandle_t s_lock = nullptr;
/* A custom backend probes the volume after tpl_sd_setup returns. Walking the
 * FAT inside the mount runs before that probe. The GPIO backend has no probe
 * and still records sizes here. */
static bool s_defer_usage = false;

bool tpl_sd_initialized = false;
char tpl_sd_status[64] = "SD: not initialized";

bool tpl_sd_lock(uint32_t timeout_ms) {
  if (!s_lock) s_lock = xSemaphoreCreateRecursiveMutex();
  if (!s_lock) return false;
  return xSemaphoreTakeRecursive(s_lock, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void tpl_sd_unlock() {
  if (s_lock) xSemaphoreGiveRecursive(s_lock);
}

static void sd_spi_reset() {
  pinMode(TPL_SD_CS, OUTPUT);
  digitalWrite(TPL_SD_CS, HIGH);
  s_default_spi.beginTransaction(
      SPISettings(TPL_SD_SPI_FREQ, MSBFIRST, SPI_MODE0));
  for (int i = 0; i < 10; i++) {
    s_default_spi.transfer(0xFF);
  }
  s_default_spi.endTransaction();
}

static bool sd_default_begin() {
  s_default_spi.begin(TPL_SD_CLK, TPL_SD_MISO, TPL_SD_MOSI, TPL_SD_CS);
  sd_spi_reset();
  return true;
}

static void sd_default_end() { s_default_spi.end(); }

static const TplSdBackend s_default_backend = {
    &s_default_spi, TPL_SD_CS, sd_default_begin, sd_default_end};

static TplSdBackend s_backend = s_default_backend;

static void sd_close_bus() {
  if (!s_bus_open) return;
  if (s_backend.end) s_backend.end();
  s_bus_open = false;
}

static bool sd_init_card() {
  if (!s_backend.spi || !s_backend.begin) return false;
  if (!s_backend.begin()) return false;
  s_bus_open = true;
  if (!SD.begin(s_backend.ss_gpio, *s_backend.spi, TPL_SD_SPI_FREQ)) {
    sd_close_bus();
    return false;
  }
  return true;
}

static const char* sd_type_name(uint8_t cardType) {
  if (cardType == CARD_MMC) return "MMC";
  if (cardType == CARD_SD) return "SDSC";
  if (cardType == CARD_SDHC) return "SDHC";
  return "UNKNOWN";
}

static void sd_update_status() {
  uint8_t cardType = SD.cardType();
  const char* type_str = sd_type_name(cardType);
  if (s_defer_usage) {
    snprintf(tpl_sd_status, sizeof(tpl_sd_status), "SD: %s", type_str);
    TPL_LOGI("sd", "[SD] Type: %s", type_str);
    return;
  }
  uint64_t total = SD.totalBytes() / (1024 * 1024);
  uint64_t used = SD.usedBytes() / (1024 * 1024);
  snprintf(tpl_sd_status, sizeof(tpl_sd_status), "SD: %s %lluMB/%lluMB",
           type_str, used, total);
  TPL_LOGI("sd", "[SD] Type: %s, Size: %lluMB, Used: %lluMB", type_str, total,
           used);
}

static bool sd_mount() {
  if (!sd_init_card()) {
    TPL_LOGI("sd", "[SD] Card Mount Failed");
    snprintf(tpl_sd_status, sizeof(tpl_sd_status), "SD: mount failed");
    return false;
  }

  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    TPL_LOGI("sd", "[SD] No SD card attached");
    SD.end();
    sd_close_bus();
    snprintf(tpl_sd_status, sizeof(tpl_sd_status), "SD: no card");
    return false;
  }

  tpl_sd_initialized = true;
  sd_update_status();
  return true;
}

static String sd_list_dir_unlocked(const char* dirname) {
  String result = "[";
  File root = SD.open(dirname);
  if (!root || !root.isDirectory()) {
    return "[{\"error\":\"cannot open dir\"}]";
  }

  File file = root.openNextFile();
  bool first = true;
  while (file) {
    if (!first) result += ",";
    first = false;
    result += "{\"name\":\"" + String(file.name()) + "\"";
    result += ",\"size\":" + String(file.size());
    result += ",\"dir\":" + String(file.isDirectory() ? "true" : "false");
    result += "}";
    file = root.openNextFile();
  }
  result += "]";
  return result;
}

static void sd_shutdown_unlocked() {
  if (tpl_sd_initialized) {
    SD.end();
    tpl_sd_initialized = false;
  }
  sd_close_bus();
  snprintf(tpl_sd_status, sizeof(tpl_sd_status), "SD: not initialized");
}

bool tpl_sd_setup(const TplSdBackend* backend) {
  if (!tpl_sd_lock(2000)) return false;
  if (tpl_sd_initialized || s_bus_open) sd_shutdown_unlocked();
  s_backend = backend ? *backend : s_default_backend;
  s_defer_usage = (backend != nullptr);
  const bool ok = sd_mount();
  tpl_sd_unlock();
  return ok;
}

String tpl_sd_list_dir(const char* dirname, uint8_t levels) {
  if (!tpl_sd_lock(2000)) return "[{\"error\":\"SD busy\"}]";
  String result = sd_list_dir_unlocked(dirname);
  tpl_sd_unlock();
  return result;
}

bool tpl_sd_end() {
  if (!tpl_sd_lock(2000)) return false;
  sd_shutdown_unlocked();
  tpl_sd_unlock();
  return true;
}

bool tpl_sd_reinit() {
  if (!tpl_sd_lock(2000)) return false;
  sd_shutdown_unlocked();
  const bool ok = sd_mount();
  tpl_sd_unlock();
  return ok;
}

namespace {
struct SdLockGuard {
  bool locked;
  explicit SdLockGuard(uint32_t timeout_ms) : locked(tpl_sd_lock(timeout_ms)) {}
  ~SdLockGuard() {
    if (locked) tpl_sd_unlock();
  }
};
}  // namespace

void tpl_sd_register_endpoints(bool include_status, bool include_reinit) {
  if (include_status) {
    tpl_server.on("/sd/status", HTTP_GET, []() {
      tpl_server.sendHeader("Access-Control-Allow-Origin", "*");
      tpl_server.sendHeader("Connection", "close");
      char buf[256];
      if (tpl_sd_initialized) {
        SdLockGuard lock(2000);
        if (!lock.locked) {
          tpl_server.send(503, "application/json",
                          "{\"error\":\"SD busy\"}");
          return;
        }
        uint64_t total = SD.totalBytes();
        uint64_t used = SD.usedBytes();
        snprintf(buf, sizeof(buf),
                 "{\"initialized\":true,\"type\":\"%s\",\"total\":%llu,"
                 "\"used\":%llu,\"free\":%llu}",
                 tpl_sd_status, total, used, total - used);
      } else {
        snprintf(buf, sizeof(buf), "{\"initialized\":false,\"status\":\"%s\"}",
                 tpl_sd_status);
      }
      tpl_server.send(200, "application/json", buf);
    });
  }

  tpl_server.on("/sd/list", HTTP_GET, []() {
    tpl_server.sendHeader("Access-Control-Allow-Origin", "*");
    tpl_server.sendHeader("Connection", "close");
    if (!tpl_sd_initialized) {
      tpl_server.send(503, "application/json",
                      "{\"error\":\"SD not initialized\"}");
      return;
    }
    SdLockGuard lock(2000);
    if (!lock.locked) {
      tpl_server.send(503, "application/json", "{\"error\":\"SD busy\"}");
      return;
    }
    String path = tpl_server.hasArg("path") ? tpl_server.arg("path") : "/";
    String result = sd_list_dir_unlocked(path.c_str());
    tpl_server.send(200, "application/json", result);
  });

  tpl_server.on("/sd/read", HTTP_GET, []() {
    tpl_server.sendHeader("Access-Control-Allow-Origin", "*");
    tpl_server.sendHeader("Connection", "close");
    if (!tpl_sd_initialized) {
      tpl_server.send(503, "application/json",
                      "{\"error\":\"SD not initialized\"}");
      return;
    }
    if (!tpl_server.hasArg("file")) {
      tpl_server.send(400, "text/plain", "Missing 'file' parameter");
      return;
    }
    String filename = tpl_server.arg("file");
    File readFile;
    size_t remain = 0;
    {
      SdLockGuard lock(2000);
      if (!lock.locked) {
        tpl_server.send(503, "application/json", "{\"error\":\"SD busy\"}");
        return;
      }
      if (!SD.exists(filename)) {
        tpl_server.send(404, "text/plain", "File not found: " + filename);
        return;
      }
      readFile = SD.open(filename, "r");
      if (!readFile || readFile.isDirectory()) {
        if (readFile) readFile.close();
        tpl_server.send(404, "text/plain", "Cannot open: " + filename);
        return;
      }
      if (readFile.size() > TPL_SD_READ_MAX) {
        readFile.close();
        tpl_server.send(413, "text/plain", "File too large for /sd/read");
        return;
      }
      remain = readFile.size();
    }
    /* The body is arbitrary bytes (day files are binary). Send it only after
     * the card lock is released, in the same chunks as /sd/download. */
    tpl_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    tpl_server.send(200, "application/octet-stream", "");
    char buf[1024];
    while (remain > 0) {
      size_t n;
      {
        SdLockGuard lock(2000);
        if (!lock.locked) break;
        const size_t want = remain < sizeof(buf) ? remain : sizeof(buf);
        n = readFile.read((uint8_t*)buf, want);
      }
      if (n == 0) break;
      remain -= n;
      tpl_server.sendContent(buf, n);
    }
    {
      SdLockGuard lock(2000);
      if (lock.locked) readFile.close();
    }
    tpl_server.sendContent("");
  });

  tpl_server.on("/sd/download", HTTP_GET, []() {
    tpl_server.sendHeader("Access-Control-Allow-Origin", "*");
    tpl_server.sendHeader("Connection", "close");
    if (!tpl_sd_initialized) {
      tpl_server.send(503, "application/json",
                      "{\"error\":\"SD not initialized\"}");
      return;
    }
    if (!tpl_server.hasArg("file")) {
      tpl_server.send(400, "text/plain", "Missing 'file' parameter");
      return;
    }
    String filename = tpl_server.arg("file");
    File downloadFile;
    {
      SdLockGuard lock(2000);
      if (!lock.locked) {
        tpl_server.send(503, "application/json", "{\"error\":\"SD busy\"}");
        return;
      }
      if (!SD.exists(filename)) {
        tpl_server.send(404, "text/plain", "File not found: " + filename);
        return;
      }
      downloadFile = SD.open(filename, "r");
      if (!downloadFile) {
        tpl_server.send(404, "text/plain", "Cannot open: " + filename);
        return;
      }
    }
    tpl_server.sendHeader("Content-Disposition",
                          "attachment; filename=\"" + filename + "\"");
    tpl_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    tpl_server.send(200, "application/octet-stream", "");
    /* Read in small chunks so a download does not hold the card lock for its
     * whole duration; the recorder can take it between chunks. */
    char buf[1024];
    while (downloadFile) {
      size_t n;
      {
        SdLockGuard lock(2000);
        if (!lock.locked) break;
        n = downloadFile.read((uint8_t*)buf, sizeof(buf));
      }
      if (n == 0) break;
      tpl_server.sendContent(buf, n);
    }
    {
      SdLockGuard lock(2000);
      if (lock.locked) downloadFile.close();
    }
    tpl_server.sendContent("");
  });

  auto sd_delete = []() {
    tpl_server.sendHeader("Access-Control-Allow-Origin", "*");
    tpl_server.sendHeader("Connection", "close");
    if (!tpl_sd_initialized) {
      tpl_server.send(503, "application/json",
                      "{\"error\":\"SD not initialized\"}");
      return;
    }
    if (!tpl_server.hasArg("file")) {
      tpl_server.send(400, "text/plain", "Missing 'file' parameter");
      return;
    }
    String filename = tpl_server.arg("file");
    if (!tpl_fs_delete_allowed(filename.c_str())) {
      tpl_server.send(403, "text/plain", "refused");
      return;
    }
    SdLockGuard lock(2000);
    if (!lock.locked) {
      tpl_server.send(503, "application/json", "{\"error\":\"SD busy\"}");
      return;
    }
    if (!SD.exists(filename)) {
      tpl_server.send(404, "text/plain", "File not found: " + filename);
      return;
    }
    if (!SD.remove(filename)) {
      tpl_server.send(500, "application/json",
                      "{\"ok\":false,\"error\":\"delete failed\"}");
      return;
    }
    tpl_server.send(200, "application/json", "{\"ok\":true}");
  };
  tpl_server.on("/sd/delete", HTTP_POST, sd_delete);

  if (include_reinit) {
    tpl_server.on("/sd/reinit", HTTP_GET, []() {
      tpl_server.sendHeader("Access-Control-Allow-Origin", "*");
      tpl_server.sendHeader("Connection", "close");
      if (tpl_sd_reinit()) {
        tpl_server.send(200, "application/json",
                        "{\"ok\":true,\"status\":\"SD reinitialized\"}");
      } else {
        tpl_server.send(500, "application/json",
                        "{\"ok\":false,\"error\":\"SD mount failed\"}");
      }
    });
  }
}

#endif
