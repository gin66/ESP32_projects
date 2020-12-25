#include "tpl_system.h"

#include "rom/rtc.h"

RTC_ATTR uint16_t rtc_watchpoint;

struct tpl_task_s tpl_tasks = {.task_wifi_manager = NULL,
                               .task_net_watchdog = NULL,
                               .task_websocket = NULL,
                               .task_webserver = NULL,
                               .task_command = NULL,
                               .task_telegram = NULL,
                               .task_app1 = NULL,
                               .app_name1 = NULL,
                               .task_app2 = NULL,
                               .app_name2 = NULL};

static char stack_info[] =
    "-----------------------------------------------------------"
    "----------------------------------------------------"
    "----------------------------------------------------";

struct tpl_config_s tpl_config = {.bootCount = 0,
                                  .allow_deepsleep = false,
                                  .ota_ongoing = false,
#ifdef IS_ESP32CAM
                                  .ws_send_jpg_image = false,
                                  .bot_send_jpg_image = false,
#endif
#ifdef BOTtoken
                                  .receive_bot_token = BOTtoken,
                                  .send_bot_token = BOTtoken,
                                  .bot_message = NULL,
                                  .bot_send_message = false,
#endif
                                  .reset_reason = 0,
                                  .reset_reason_cpu0 = "",
                                  .reset_reason_cpu1 = "",
                                  .deepsleep_time_secs = 0,
                                  .stack_info = stack_info,
                                  .curr_jpg = NULL,
                                  .curr_jpg_len = 0,
                                  .last_seen_watchpoint = 0,
                                  .watchpoint = 0};

#define ADD_STACK_INFO(name, task)                                        \
  {                                                                       \
    if (task != NULL) {                                                   \
      p += sprintf(p, " %s=%d", name, uxTaskGetStackHighWaterMark(task)); \
    }                                                                     \
  }
void tpl_update_stack_info() {
  char *p = stack_info;
  p += sprintf(p, "Stackfree: this=%d", uxTaskGetStackHighWaterMark(NULL));
  ADD_STACK_INFO("net_watchdog", tpl_tasks.task_net_watchdog);
  ADD_STACK_INFO("websocket", tpl_tasks.task_websocket);
  ADD_STACK_INFO("command", tpl_tasks.task_command);
  ADD_STACK_INFO("wifi", tpl_tasks.task_wifi_manager);
  ADD_STACK_INFO("http", tpl_tasks.task_webserver);
  ADD_STACK_INFO("bot", tpl_tasks.task_telegram);
  ADD_STACK_INFO(tpl_tasks.app_name1, tpl_tasks.task_app1);
  ADD_STACK_INFO(tpl_tasks.app_name2, tpl_tasks.task_app2);
}

static RTC_DATA_ATTR uint32_t bootCount = 0;

static const char *reason[] = {
    "OK",
    "POWERON_RESET",
    "?",
    "SW_RESET",
    "OWDT_RESET",
    "DEEPSLEEP_RESET",
    "SDIO_RESET",
    "TG0WDT_SYS_RESET",
    "TG1WDT_SYS_RESET",
    "RTCWDT_SYS_RESET",
    "INTRUSION_RESET",
    "TGWDT_CPU_RESET",
    "SW_CPU_RESET",
    "RTCWDT_CPU_RESET",
    "EXT_CPU_RESET",
    "RTCWDT_BROWN_OUT_RESET",
    "RTCWDT_RTC_RESET",
};

void tpl_system_setup(uint32_t deep_sleep_secs) {
  bootCount++;
  tpl_config.bootCount = bootCount;
  uint16_t r0 = rtc_get_reset_reason(0);
  uint16_t r1 = rtc_get_reset_reason(1);
  tpl_config.reset_reason = (r0 << 8) | r1;
  if (r0 <= 16) {
    tpl_config.reset_reason_cpu0 = reason[r0];
  }
  if (r1 <= 16) {
    tpl_config.reset_reason_cpu1 = reason[r1];
  }
  tpl_config.deepsleep_time_secs = deep_sleep_secs;
  if (deep_sleep_secs == 0) {
    tpl_config.allow_deepsleep = false;
  } else {
    tpl_config.allow_deepsleep = true;
  }
}
