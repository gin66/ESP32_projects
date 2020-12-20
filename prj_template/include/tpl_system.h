#include <Arduino.h>

#define CORE_0 0
#define CORE_1 1

extern struct tpl_task_s {
	TaskHandle_t task_net_watchdog;
} tpl_tasks;
