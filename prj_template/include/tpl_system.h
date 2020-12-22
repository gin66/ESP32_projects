#include <Arduino.h>

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
  char *bot_message_80;
  volatile bool bot_send_message;
  uint16_t reset_reason;
  const char *reset_reason_cpu0;
  const char *reset_reason_cpu1;
  uint64_t deepsleep_time;  // =0 means off
  const char *stack_info;
  uint8_t *curr_jpg;
  size_t curr_jpg_len;
} tpl_config;

void tpl_update_stack_info();
void tpl_system_setup();
