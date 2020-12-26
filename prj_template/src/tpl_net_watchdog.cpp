#include "tpl_net_watchdog.h"

#include <esp_task_wdt.h>
#include <stdint.h>

#include "tpl_system.h"

#ifdef NET_WATCHDOG
// Arduino:
//   Task watchdog 5s
//   Interrupt watchdog 300ms

#include <ESP32Ping.h>
uint8_t tpl_fail = 0;
void TaskWatchdog(void* pvParameters) {
  const TickType_t xDelay = 1000 / portTICK_PERIOD_MS;
  for (;;) {
    esp_task_wdt_reset();
    bool success = Ping.ping(NET_WATCHDOG, 1);
    if (!success) {
      tpl_fail++;
      Serial.print("Ping failed: ");
      Serial.println(tpl_fail);
      if (tpl_fail >= 5 * 60) {  // 5 minutes
        ESP.restart();
        // deep sleep without wifi stop may cause the state,
        // that webserver/sockets/ota/watchdog are dead,
        // ping reply is working, and esp32 lingers in this state till
        // reset or power cycle. So better use restart() here
        // esp_sleep_enable_timer_wakeup(1LL * 1000000LL);
        // esp_deep_sleep_start();
      }
    } else {
      tpl_fail = 0;
    }
    vTaskDelay(xDelay);
  }
}

bool tpl_net_watchdog_setup() {
  BaseType_t rc;

  rc = xTaskCreatePinnedToCore(TaskWatchdog, "Net Watchdog", 2688, NULL, 0,
                               &tpl_tasks.task_net_watchdog, CORE_0);
  if (rc == pdPASS) {
    // Add this task to task watchdog
    esp_task_wdt_add(tpl_tasks.task_net_watchdog);
  }
  return (rc == pdPASS);
}
#elif NO_NET_WATCHDOG
#else
#error "Need to define either NET_WATCHDOG=<ip> or NO_NET_WATCHDOG"
#endif
