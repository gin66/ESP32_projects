#include <Arduino.h>

#define CORE_0 0
#define CORE_1 1

#ifdef IS_ESP32CAM
//#define ledPin ((gpio_num_t)33)
//#define flashPin ((gpio_num_t)4)
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
  uint16_t reset_reason;
  const char *stack_info;
} tpl_config;

void tpl_update_stack_info();
void tpl_system_setup();
