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

void tpl_update_stack_info() {
	sprintf(stack_info,
    "Stackfree: loop=%d" \
    " net_watchdog=%d" \
    " websocket=%d" \
    " command=%d" \
    " wifi=%d" \
    " http=%d" \
    " bot=%d",
    uxTaskGetStackHighWaterMark(NULL),
    uxTaskGetStackHighWaterMark(tpl_tasks.task_net_watchdog),
    uxTaskGetStackHighWaterMark(tpl_tasks.task_websocket),
    uxTaskGetStackHighWaterMark(tpl_tasks.task_command),
    uxTaskGetStackHighWaterMark(tpl_tasks.task_wifi_manager),
    uxTaskGetStackHighWaterMark(tpl_tasks.task_webserver),
    uxTaskGetStackHighWaterMark(tpl_tasks.task_telegram));
}

static RTC_DATA_ATTR uint32_t bootCount = 0;

void tpl_system_setup() {
   bootCount++;
   tpl_config.bootCount = bootCount;
}
