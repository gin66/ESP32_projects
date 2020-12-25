#undef ARDUINOJSON_USE_LONG_LONG
#include "tpl_command.h"

#include <Arduino.h>
#include <esp_wifi.h>

#include "tpl_esp_camera.h"
#include "tpl_system.h"

volatile enum Command tpl_command = CmdIdle;

#ifdef IS_ESP32CAM
void psram_flush_cache() {
  volatile uint8_t *psram = (volatile uint8_t *)SOC_EXTRAM_DATA_LOW;
  /*
   *     Single-core and even/odd mode only have 32K of cache evenly
   * distributed over the address lines. We can clear the cache by just
   * reading 64K worth of cache lines.
   *             */
  uint8_t scrap = 0;
  for (uint32_t x = 0; x < 1024 * 64; x += 32) {
    scrap += psram[x];
    scrap += psram[x + (1024 * 1024 * 2)];
  }
}
#endif

void TaskCommandCore1(void *pvParameters) {
  const TickType_t xDelay = 10 / portTICK_PERIOD_MS;
  void (*func)(enum Command command) = (void (*)(enum Command))pvParameters;

  for (;;) {
    switch (tpl_command) {
      case CmdIdle:
        break;
#ifdef IS_ESP32CAM
      case CmdSendJpg2Ws:
        tpl_config.ws_send_jpg_image = true;
        tpl_command = CmdIdle;
        break;
      case CmdSendJpg2Bot:
        tpl_config.bot_send_jpg_image = true;
        tpl_command = CmdWaitSendJpg2Bot;
        break;
      case CmdWaitSendJpg2Bot:
        if (!tpl_config.bot_send_jpg_image) {
          tpl_command = CmdIdle;
        }
        break;
      case CmdFlash:
        digitalWrite(tpl_flashPin, !digitalRead(tpl_flashPin));
        tpl_command = CmdIdle;
        break;
#endif
      case CmdDeepSleep:
        if (tpl_config.allow_deepsleep && !tpl_config.ota_ongoing &&
            (tpl_config.deepsleep_time_secs != 0)) {
          vTaskSuspend(tpl_tasks.task_wifi_manager);
		  if (tpl_tasks.task_telegram) {
            vTaskSuspend(tpl_tasks.task_telegram);
		  }
		  Serial.print("Wifi stop:");
          Serial.println(esp_wifi_stop());
          Serial.println(esp_wifi_stop());
#ifdef IS_ESP32CAM
          Serial.println("Prepare esp32cam for deepsleep");
          tpl_camera_off();
          Serial.println("Start psram flush");
          psram_flush_cache();
#endif
          uint64_t sleep = tpl_config.deepsleep_time_secs;
          sleep *= 1000000LL;
          esp_sleep_enable_timer_wakeup(sleep);
		  Serial.println("Enter sleep");
          esp_deep_sleep_start();
        }
        tpl_command = CmdIdle;
        break;
      default:
        if (func != NULL) {
          func(tpl_command);
        }
    }
    vTaskDelay(xDelay);
  }
}
//---------------------------------------------------
void tpl_command_setup(void (*func)(enum Command command)) {
  xTaskCreatePinnedToCore(TaskCommandCore1, "Command", 2048, (void *)func, 1,
                          &tpl_tasks.task_command, CORE_1);
}
