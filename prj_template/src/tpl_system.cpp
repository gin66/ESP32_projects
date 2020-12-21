#include "tpl_system.h"

struct tpl_task_s tpl_tasks = {.task_wifi_manager = NULL,
                               .task_net_watchdog = NULL,
                               .task_websocket = NULL,
                               .task_webserver = NULL,
                               .task_command = NULL,
                               .task_telegram = NULL};

static char stack_info[] = "-----------------------------------------------------------"\
	"----------------------------------------------------"\
	"----------------------------------------------------";

struct tpl_config_s tpl_config = {.bootCount = 0,
								  .allow_deepsleep = false,
                                  .ota_ongoing = false,
								  .stack_info = stack_info};

#define ADD_STACK_INFO(info_fmt,task) {\
	if(task != NULL) {\
		p += sprintf(p, info_fmt, uxTaskGetStackHighWaterMark(task));\
	}\
}
void tpl_update_stack_info() {
	char *p = stack_info;
	p += sprintf(p, "Stackfree: this=%d", uxTaskGetStackHighWaterMark(NULL));
    ADD_STACK_INFO(" net_watchdog=%d",tpl_tasks.task_net_watchdog);
    ADD_STACK_INFO(" websocket=%d",tpl_tasks.task_websocket);
    ADD_STACK_INFO(" command=%d",tpl_tasks.task_command);
    ADD_STACK_INFO(" wifi=%d",tpl_tasks.task_wifi_manager);
    ADD_STACK_INFO(" http=%d",tpl_tasks.task_webserver);
    ADD_STACK_INFO(" bot=%d",tpl_tasks.task_telegram);
}

static RTC_DATA_ATTR uint32_t bootCount = 0;

void tpl_system_setup() {
   bootCount++;
   tpl_config.bootCount = bootCount;
}
