#include <Arduino.h>

#include "../../private_bot.h"
#include "../../private_sha.h"

#define CORE_0 0
#define CORE_1 1

#ifdef IS_ESP32CAM
#define tpl_ledPin 33
#define tpl_flashPin 4
#endif

extern struct tpl_task_s {
  TaskHandle_t task_wifi_manager;
  TaskHandle_t task_net_watchdog;
  TaskHandle_t task_websocket;
  TaskHandle_t task_webserver;
  TaskHandle_t task_command;
  TaskHandle_t task_telegram;
  TaskHandle_t task_app1;
  const char *app_name1;
  TaskHandle_t task_app2;
  const char *app_name2;
} tpl_tasks;

extern struct tpl_config_s {
  uint32_t bootCount;
  volatile bool allow_deepsleep;
  volatile bool ota_ongoing;
#ifdef IS_ESP32CAM
  volatile bool ws_send_jpg_image;
  volatile bool bot_send_jpg_image;
#endif
#ifdef BOTtoken
  const char *receive_bot_token;  // do not change
  const char *send_bot_token;
  const char *bot_message;
  volatile bool bot_send_message;
#endif
  uint16_t reset_reason;
  const char *reset_reason_cpu0;
  const char *reset_reason_cpu1;
  uint32_t deepsleep_time_secs;  // =0 means off
  const char *stack_info;
  uint8_t *curr_jpg;
  size_t curr_jpg_len;
  uint32_t last_seen_watchpoint;
  uint32_t watchpoint;
} tpl_config;

void tpl_update_stack_info();
void tpl_system_setup(uint32_t deep_sleep_secs = 0);

extern RTC_DATA_ATTR uint16_t rtc_watchpoint;
#define WATCH(i)               \
  {                            \
    rtc_watchpoint = i;        \
    tpl_config.watchpoint = i; \
  }
