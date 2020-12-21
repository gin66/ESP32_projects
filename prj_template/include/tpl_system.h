#include <Arduino.h>

#define CORE_0 0
#define CORE_1 1

extern struct tpl_task_s {
  TaskHandle_t task_wifi_manager;
  TaskHandle_t task_net_watchdog;
  TaskHandle_t task_websocket;
  TaskHandle_t task_webserver;
  TaskHandle_t task_command;
  TaskHandle_t task_telegram;
} tpl_tasks;

extern struct tpl_config_s {
  bool allow_deepsleep;
  bool ota_ongoing;
  const char *stack_info;
} tpl_config;

void tpl_update_stack_info();
