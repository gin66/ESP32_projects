#include "tpl_net_watchdog.h"

#include <stdint.h>

#ifdef NET_WATCHDOG
#include <ESP32Ping.h>
uint8_t fail = 0;
void TaskWatchdog(void* pvParameters) {
  const TickType_t xDelay = 1000 / portTICK_PERIOD_MS;
  for (;;) {
    bool success = Ping.ping(NET_WATCHDOG, 1);
    if (!success) {
      fail++;
      Serial.println("Ping failed");
      if (fail >= 100) {
        ESP.restart();
      }
    } else {
      fail = 0;
    }
    vTaskDelay(xDelay);
  }
}

bool startNetWatchDog() {
  BaseType_t rc;

  rc = xTaskCreatePinnedToCore(TaskWatchdog, "Net Watchdog", 4096, (void*)1, 1,
                               NULL, 0);
  return (rc == pdPASS);
}
#elif NO_NET_WATCHDOG
#else
#error "Need to define either NET_WATCHDOG=<ip> or NO_NET_WATCHDOG"
#endif
