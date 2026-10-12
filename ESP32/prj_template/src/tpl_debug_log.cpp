#include "tpl_debug_log.h"
#include "tpl_webserver.h"

#ifdef USE_TPL_DEBUG_LOG

#include <esp_log.h>

static char log_buf[DEBUG_LOG_LINES][DEBUG_LOG_LINE_SIZE];
static int log_head = 0;
static int log_count = 0;
static SemaphoreHandle_t log_mutex = NULL;

static int debug_log_vprintf(const char* fmt, va_list args) {
  char tmp[DEBUG_LOG_LINE_SIZE];
  int len = vsnprintf(tmp, sizeof(tmp), fmt, args);
  Serial.print(tmp);

  if (log_mutex && xSemaphoreTake(log_mutex, pdMS_TO_TICKS(10))) {
    unsigned long ms = millis();
    snprintf(log_buf[log_head], DEBUG_LOG_LINE_SIZE, "[%lu.%03lu] %s",
             ms / 1000, ms % 1000, tmp);
    log_head = (log_head + 1) % DEBUG_LOG_LINES;
    if (log_count < DEBUG_LOG_LINES) log_count++;
    xSemaphoreGive(log_mutex);
  }

  return len;
}

void tpl_debug_log_setup() {
  log_mutex = xSemaphoreCreateMutex();
  esp_log_set_vprintf(debug_log_vprintf);
}

void tpl_debug_log_register_endpoints() {
  tpl_server.on("/log", HTTP_GET, []() {
    tpl_server.sendHeader("Access-Control-Allow-Origin", "*");

    if (!log_mutex) {
      tpl_server.send(200, "text/plain", "debug log not initialized");
      return;
    }

    xSemaphoreTake(log_mutex, portMAX_DELAY);
    int start;
    int n;
    if (log_count < DEBUG_LOG_LINES) {
      start = 0;
      n = log_count;
    } else {
      start = log_head;
      n = DEBUG_LOG_LINES;
    }

    tpl_server.setContentSize(CONTENT_SIZE_UNKNOWN);
    tpl_server.send(200, "text/plain", "");

    for (int i = 0; i < n; i++) {
      int idx = (start + i) % DEBUG_LOG_LINES;
      tpl_server.sendContent(log_buf[idx]);
      tpl_server.sendContent("\n");
    }
    xSemaphoreGive(log_mutex);
    tpl_server.sendContent("");
  });

  tpl_server.on("/log/clear", HTTP_GET, []() {
    if (log_mutex) {
      xSemaphoreTake(log_mutex, portMAX_DELAY);
      log_head = 0;
      log_count = 0;
      xSemaphoreGive(log_mutex);
    }
    tpl_server.sendHeader("Access-Control-Allow-Origin", "*");
    tpl_server.send(200, "application/json", "{\"ok\":true}");
  });
}

#else

void tpl_debug_log_setup() {}

void tpl_debug_log_register_endpoints() {
  tpl_server.on("/log", HTTP_GET, []() {
    tpl_server.sendHeader("Access-Control-Allow-Origin", "*");
    tpl_server.send(200, "text/plain",
                    "debug log disabled (compile with -DUSE_TPL_DEBUG_LOG to enable)");
  });
}

#endif
