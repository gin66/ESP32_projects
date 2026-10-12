#include "tpl_sd.h"

#ifdef USE_TPL_SD

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

static SPIClass sdSPI(VSPI);

bool tpl_sd_initialized = false;
char tpl_sd_status[64] = "SD: not initialized";

static void sd_spi_reset() {
  pinMode(TPL_SD_CS, OUTPUT);
  digitalWrite(TPL_SD_CS, HIGH);
  sdSPI.beginTransaction(SPISettings(TPL_SD_SPI_FREQ, MSBFIRST, SPI_MODE0));
  for (int i = 0; i < 10; i++) {
    sdSPI.transfer(0xFF);
  }
  sdSPI.endTransaction();
}

static bool sd_init_card() {
  sdSPI.begin(TPL_SD_CLK, TPL_SD_MISO, TPL_SD_MOSI, TPL_SD_CS);
  sd_spi_reset();
  return SD.begin(TPL_SD_CS, sdSPI, TPL_SD_SPI_FREQ);
}

static void sd_update_status() {
  uint8_t cardType = SD.cardType();
  const char* type_str = "UNKNOWN";
  if (cardType == CARD_MMC) type_str = "MMC";
  else if (cardType == CARD_SD) type_str = "SDSC";
  else if (cardType == CARD_SDHC) type_str = "SDHC";
  uint64_t total = SD.totalBytes() / (1024 * 1024);
  uint64_t used = SD.usedBytes() / (1024 * 1024);
  snprintf(tpl_sd_status, sizeof(tpl_sd_status), "SD: %s %lluMB/%lluMB",
           type_str, used, total);
}

bool tpl_sd_setup() {
  if (!sd_init_card()) {
    Serial.println("[SD] Card Mount Failed");
    snprintf(tpl_sd_status, sizeof(tpl_sd_status), "SD: mount failed");
    return false;
  }

  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("[SD] No SD card attached");
    snprintf(tpl_sd_status, sizeof(tpl_sd_status), "SD: no card");
    return false;
  }

  tpl_sd_initialized = true;
  sd_update_status();

  const char* type_str = "UNKNOWN";
  if (cardType == CARD_MMC) type_str = "MMC";
  else if (cardType == CARD_SD) type_str = "SDSC";
  else if (cardType == CARD_SDHC) type_str = "SDHC";

  Serial.printf("[SD] Type: %s, Size: %lluMB, Used: %lluMB\n",
                type_str, SD.totalBytes() / (1024 * 1024),
                SD.usedBytes() / (1024 * 1024));
  return true;
}

String tpl_sd_list_dir(const char* dirname, uint8_t levels) {
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

bool tpl_sd_reinit() {
  if (tpl_sd_initialized) {
    SD.end();
    sdSPI.end();
  }
  tpl_sd_initialized = false;
  if (sd_init_card()) {
    sd_update_status();
    tpl_sd_initialized = true;
    return true;
  } else {
    snprintf(tpl_sd_status, sizeof(tpl_sd_status), "SD: mount failed");
    return false;
  }
}

void tpl_sd_register_endpoints() {
  tpl_server.on("/sd/list", HTTP_GET, []() {
    tpl_server.sendHeader("Access-Control-Allow-Origin", "*");
    tpl_server.sendHeader("Connection", "close");
    if (!tpl_sd_initialized) {
      tpl_server.send(503, "application/json", "{\"error\":\"SD not initialized\"}");
      return;
    }
    String path = tpl_server.hasArg("path") ? tpl_server.arg("path") : "/";
    String result = tpl_sd_list_dir(path.c_str(), 0);
    tpl_server.send(200, "application/json", result);
  });

  tpl_server.on("/sd/read", HTTP_GET, []() {
    tpl_server.sendHeader("Access-Control-Allow-Origin", "*");
    tpl_server.sendHeader("Connection", "close");
    if (!tpl_sd_initialized) {
      tpl_server.send(503, "application/json", "{\"error\":\"SD not initialized\"}");
      return;
    }
    if (!tpl_server.hasArg("file")) {
      tpl_server.send(400, "text/plain", "Missing 'file' parameter");
      return;
    }
    String filename = tpl_server.arg("file");
    if (!SD.exists(filename)) {
      tpl_server.send(404, "text/plain", "File not found: " + filename);
      return;
    }
    File f = SD.open(filename, "r");
    String content = f.readString();
    f.close();
    tpl_server.send(200, "text/html", content);
  });

  tpl_server.on("/sd/download", HTTP_GET, []() {
    tpl_server.sendHeader("Access-Control-Allow-Origin", "*");
    if (!tpl_sd_initialized) {
      tpl_server.sendHeader("Connection", "close");
      tpl_server.send(503, "application/json", "{\"error\":\"SD not initialized\"}");
      return;
    }
    if (!tpl_server.hasArg("file")) {
      tpl_server.sendHeader("Connection", "close");
      tpl_server.send(400, "text/plain", "Missing 'file' parameter");
      return;
    }
    String filename = tpl_server.arg("file");
    if (!SD.exists(filename)) {
      tpl_server.sendHeader("Connection", "close");
      tpl_server.send(404, "text/plain", "File not found: " + filename);
      return;
    }
    File downloadFile = SD.open(filename, "r");
    tpl_server.sendHeader("Connection", "close");
    tpl_server.sendHeader("Content-Disposition",
                          "attachment; filename=\"" + filename + "\"");
    tpl_server.streamFile(downloadFile, "application/octet-stream");
    downloadFile.close();
  });

  tpl_server.on("/sd/status", HTTP_GET, []() {
    tpl_server.sendHeader("Access-Control-Allow-Origin", "*");
    tpl_server.sendHeader("Connection", "close");
    char buf[256];
    if (tpl_sd_initialized) {
      uint64_t total = SD.totalBytes();
      uint64_t used = SD.usedBytes();
      snprintf(buf, sizeof(buf),
               "{\"initialized\":true,\"type\":\"%s\",\"total\":%llu,\"used\":%llu,\"free\":%llu}",
               tpl_sd_status, total, used, total - used);
    } else {
      snprintf(buf, sizeof(buf),
               "{\"initialized\":false,\"status\":\"%s\"}", tpl_sd_status);
    }
    tpl_server.send(200, "application/json", buf);
  });

  tpl_server.on("/sd/reinit", HTTP_GET, []() {
    tpl_server.sendHeader("Access-Control-Allow-Origin", "*");
    tpl_server.sendHeader("Connection", "close");
    if (tpl_sd_reinit()) {
      tpl_server.send(200, "application/json", "{\"ok\":true,\"status\":\"SD reinitialized\"}");
    } else {
      tpl_server.send(500, "application/json", "{\"ok\":false,\"error\":\"SD mount failed\"}");
    }
  });
}

#endif
