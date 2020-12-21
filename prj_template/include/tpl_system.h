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
} tpl_tasks;

extern struct tpl_config_s {
  uint32_t bootCount;
  bool allow_deepsleep;
  bool ota_ongoing;
#ifdef IS_ESP32CAM
  bool ws_send_jpg_image;
#endif
  uint16_t reset_reason;
  uint32_t deepsleep_time; // =0 means off
  const char *stack_info;
} tpl_config;

void tpl_update_stack_info();
void tpl_system_setup();
