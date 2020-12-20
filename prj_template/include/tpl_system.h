#include <Arduino.h>

#define CORE_0 0
#define CORE_1 1

extern struct tpl_task_s {
	TaskHandle_t task_wifi_manager;
	TaskHandle_t task_net_watchdog;
	TaskHandle_t task_web_socket;
	TaskHandle_t task_command;
} tpl_tasks;

extern struct tpl_config_s {
	bool allow_deepsleep;
} tpl_config;
