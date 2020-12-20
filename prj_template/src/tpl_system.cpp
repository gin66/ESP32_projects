#include "tpl_system.h"

struct tpl_task_s tpl_tasks = {
	.task_wifi_manager = NULL,
	.task_net_watchdog = NULL,
	.task_web_socket = NULL,
	.task_command = NULL
};

struct tpl_config_s tpl_config = {
	.allow_deepsleep = false
};
