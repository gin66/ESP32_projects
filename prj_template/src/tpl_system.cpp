#include "tpl_system.h"

#include <SPIFFS.h>
//#include <esp_private/esp_int_wdt.h>
#include <esp_freertos_hooks.h>
#include <esp_int_wdt.h>
#include <esp_task_wdt.h>
#include <rom/rtc.h>

RTC_DATA_ATTR struct rtc_data_s rtc_data = {
   .bootCount = 0
};

struct tpl_task_s tpl_tasks = {.task_wifi_manager = NULL,
                               .task_net_watchdog = NULL,
#ifdef MAX_ON_TIME_S
                               .task_on_time_watchdog_0 = NULL,
                               .task_on_time_watchdog_1 = NULL,
#endif
                               .task_websocket = NULL,
                               .task_webserver = NULL,
                               .task_command = NULL,
                               .task_telegram = NULL,
                               .task_app1 = NULL,
                               .app_name1 = NULL,
                               .task_app2 = NULL,
                               .app_name2 = NULL};

static char stack_info[] =
    "-----------------------------------------------------------"
    "----------------------------------------------------"
    "----------------------------------------------------";

struct tpl_config_s tpl_config = {.bootCount = 0,
                                  .allow_deepsleep = false,
                                  .ota_ongoing = false,
                                  .wifi_manager_shutdown_request = false,
                                  .wifi_manager_shutdown = false,
                                  .web_communication_ongoing = false,
                                  .ws_communication_ongoing = false,
                                  .bot_communication_ongoing = false,
#ifdef IS_ESP32CAM
                                  .ws_send_jpg_image = false,
                                  .bot_send_jpg_image = false,
#endif
#ifdef BOTtoken
                                  .receive_bot_token = BOTtoken,
                                  .send_bot_token = BOTtoken,
                                  .bot_message = NULL,
                                  .bot_send_message = false,
#endif
                                  .reset_reason = 0,
                                  .reset_reason_cpu0 = "",
                                  .reset_reason_cpu1 = "",
                                  .deepsleep_time_secs = 0,
                                  .stack_info = stack_info,
                                  .curr_jpg = NULL,
                                  .curr_jpg_len = 0,
                                  .last_seen_watchpoint = 0,
                                  .watchpoint = 0};

TplSpiffsConfig tpl_spiffs_config = TplSpiffsConfig();

#define ADD_STACK_INFO(name, task)                                        \
  {                                                                       \
    if (task != NULL) {                                                   \
      p += sprintf(p, " %s=%d", name, uxTaskGetStackHighWaterMark(task)); \
    }                                                                     \
  }
void tpl_update_stack_info() {
  char *p = stack_info;
  p += sprintf(p, "Stackfree: this=%d", uxTaskGetStackHighWaterMark(NULL));
  ADD_STACK_INFO("net_watchdog", tpl_tasks.task_net_watchdog);
#ifdef MAX_ON_TIME_S
  ADD_STACK_INFO("on_watchdog_0", tpl_tasks.task_on_time_watchdog_0);
  ADD_STACK_INFO("_1", tpl_tasks.task_on_time_watchdog_1);
#endif
  ADD_STACK_INFO("websocket", tpl_tasks.task_websocket);
  ADD_STACK_INFO("command", tpl_tasks.task_command);
  ADD_STACK_INFO("wifi", tpl_tasks.task_wifi_manager);
  ADD_STACK_INFO("http", tpl_tasks.task_webserver);
  ADD_STACK_INFO("bot", tpl_tasks.task_telegram);
  ADD_STACK_INFO(tpl_tasks.app_name1, tpl_tasks.task_app1);
  ADD_STACK_INFO(tpl_tasks.app_name2, tpl_tasks.task_app2);
}

static const char *reason[] = {
    "OK",
    "POWERON_RESET",
    "?",
    "SW_RESET",
    "OWDT_RESET",
    "DEEPSLEEP_RESET",
    "SDIO_RESET",
    "TG0WDT_SYS_RESET",
    "TG1WDT_SYS_RESET",
    "RTCWDT_SYS_RESET",
    "INTRUSION_RESET",
    "TGWDT_CPU_RESET",
    "SW_CPU_RESET",
    "RTCWDT_CPU_RESET",
    "EXT_CPU_RESET",
    "RTCWDT_BROWN_OUT_RESET",
    "RTCWDT_RTC_RESET",
};

void tpl_hard_restart() {
  // https://github.com/espressif/arduino-esp32/issues/1270
  // const esp_task_wdt_config_t twdt_config = {
  //			.timeout_ms = 1000,
  //			.idle_core_mask = (1 << configNUM_CORES) - 1,    // Bitmask of all cores,
  //			.trigger_panic = true,
  //	};
  // esp_task_wdt_reconfigure(&twdt_config);
  esp_task_wdt_init(1, true);
  esp_task_wdt_add(NULL);
  while (true)
    ;
}
#ifdef MAX_ON_TIME_S
void TaskOnTimeWatchdog(void *pvParameters) {
  Serial.println("start on time watchdog");
  const TickType_t xDelay = 1000 / portTICK_PERIOD_MS;
  for (uint8_t i = 0; i < MAX_ON_TIME_S; i++) {
    vTaskDelay(xDelay);
  }
  Serial.println("watchdog triggered");
  // cause wdt reset in case restart does not work
  esp_task_wdt_init(1, true);
  esp_task_wdt_add(NULL);
  // this finally works
  vTaskSuspendAll();
  ESP.restart();
  xTaskResumeAll();
}
#endif

uint32_t cpu_load_core0 = 0;
uint32_t cpu_load_core1 = 0;

static volatile uint32_t idle_cnt_core0 = 0;
static volatile uint32_t idle_cnt_core1 = 0;

static bool idle_hook_core0() {
  idle_cnt_core0++;
  return false;
}

static bool idle_hook_core1() {
  idle_cnt_core1++;
  return false;
}

void TaskCpuLoad(void *param) {
  (void)param;

  esp_register_freertos_idle_hook_for_cpu(idle_hook_core0, 0);
  esp_register_freertos_idle_hook_for_cpu(idle_hook_core1, 1);

  // Calibrate: measure idle counts for 1s while system is mostly idle
  idle_cnt_core0 = 0;
  idle_cnt_core1 = 0;
  vTaskDelay(1000 / portTICK_PERIOD_MS);
  uint32_t idle_max_core0 = idle_cnt_core0;
  uint32_t idle_max_core1 = idle_cnt_core1;

  while (true) {
    idle_cnt_core0 = 0;
    idle_cnt_core1 = 0;
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    uint32_t cnt0 = idle_cnt_core0;
    uint32_t cnt1 = idle_cnt_core1;

    // Self-calibrate: update max if higher idle count observed
    if (cnt0 > idle_max_core0) idle_max_core0 = cnt0;
    if (cnt1 > idle_max_core1) idle_max_core1 = cnt1;

    if (idle_max_core0 > 0) {
      cpu_load_core0 = 100 - (cnt0 * 100 / idle_max_core0);
    }
    if (idle_max_core1 > 0) {
      cpu_load_core1 = 100 - (cnt1 * 100 / idle_max_core1);
    }
  }
}

void tpl_system_setup(uint32_t deep_sleep_secs) {
  xTaskCreate(TaskCpuLoad, "CpuLoad", 2048, NULL, 1, NULL);
  rtc_data.bootCount++;
  tpl_config.bootCount = rtc_data.bootCount;
  tpl_config.last_seen_watchpoint = rtc_data.watchpoint;
  uint16_t r0 = rtc_get_reset_reason(0);
  uint16_t r1 = rtc_get_reset_reason(1);
  tpl_config.reset_reason = (r0 << 8) | r1;
  if (r0 <= 16) {
    tpl_config.reset_reason_cpu0 = reason[r0];
  }
  if (r1 <= 16) {
    tpl_config.reset_reason_cpu1 = reason[r1];
  }
  tpl_config.deepsleep_time_secs = deep_sleep_secs;
  if (deep_sleep_secs == 0) {
    tpl_config.allow_deepsleep = false;
  } else {
    tpl_config.allow_deepsleep = true;
  }
#ifdef MAX_ON_TIME_S
  xTaskCreatePinnedToCore(TaskOnTimeWatchdog, "On Time Watchdog", 1024, NULL, 0,
                          &tpl_tasks.task_on_time_watchdog_0, CORE_0);
  xTaskCreatePinnedToCore(TaskOnTimeWatchdog, "On Time Watchdog", 1024, NULL, 0,
                          &tpl_tasks.task_on_time_watchdog_1, CORE_1);
#endif
  if (SPIFFS.begin()) {
    if (SPIFFS.exists(SPIFFS_CONFIG_FNAME)) {
      File f;
      unsigned int readSize;
      f = SPIFFS.open(SPIFFS_CONFIG_FNAME, "rb");
      f.setTimeout(0);
      readSize =
          f.readBytes((char *)&tpl_spiffs_config, sizeof(TplSpiffsConfig));
      if ((readSize != sizeof(TplSpiffsConfig)) ||
          (tpl_spiffs_config.version != SPIFFS_CONFIG_VERSION)) {
        tpl_spiffs_config.init();
      }
      f.close();
    }
  }
}

bool tpl_write_config() {
  File f;
  unsigned int writeSize;

  Serial.println("Write Configuration");
  tpl_spiffs_config.need_store = false;
  f = SPIFFS.open(SPIFFS_CONFIG_FNAME, "wb");
  if (!f) {
    tpl_spiffs_config.need_store = true;
    return false;
  }
  writeSize = f.write((byte *)&tpl_spiffs_config, sizeof(TplSpiffsConfig));
  f.close();
  if (!(writeSize == sizeof(TplSpiffsConfig))) {
    tpl_spiffs_config.need_store = true;
    return false;
  }
  return true;
}
