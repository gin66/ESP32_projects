#include "tpl_net_watchdog.h"

#include <stdint.h>

#include "tpl_system.h"

#ifdef NET_WATCHDOG
#include <ESP32Ping.h>
uint8_t tpl_fail = 0;
void TaskWatchdog(void* pvParameters) {
  const TickType_t xDelay = 1000 / portTICK_PERIOD_MS;
  for (;;) {
    bool success = Ping.ping(NET_WATCHDOG, 1);
    if (!success) {
      tpl_fail++;
      Serial.print("Ping failed: ");
      Serial.println(tpl_fail);
      if (tpl_fail >= 5 * 60) {  // 5 minutes
        ESP.restart();
		// deep sleep without wifi stop may cause the state,
		// that webserver/sockets/ota/watchdog are dead,
		// ping reply is working, amd esp32 lingers in this state till
		// reset or power cycle. So better use restart() here
        //esp_sleep_enable_timer_wakeup(1LL * 1000000LL);
        //esp_deep_sleep_start();
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
  return (rc == pdPASS);
}
#elif NO_NET_WATCHDOG
#else
#error "Need to define either NET_WATCHDOG=<ip> or NO_NET_WATCHDOG"
#endif
