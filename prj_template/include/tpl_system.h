#include <Arduino.h>

#include "../../../.private/private_bot.h"
#include "../../../.private/private_sha.h"

#define CORE_0 0
#define CORE_1 1

#ifdef IS_ESP32CAM
#define tpl_ledPin 33
#define tpl_flashPin 4
#endif

#ifdef IS_ESP32CYD
#define tpl_ledPin 4
#define tpl_ledPin_R 4
#define tpl_ledPin_G 16
#define tpl_ledPin_B 17
#define tpl_backlightPin 21
#endif

extern struct tpl_task_s {
  TaskHandle_t task_wifi_manager;
  TaskHandle_t task_net_watchdog;
#ifdef MAX_ON_TIME_S
  TaskHandle_t task_on_time_watchdog_0;
  TaskHandle_t task_on_time_watchdog_1;
#endif
  TaskHandle_t task_websocket;
  TaskHandle_t task_webserver;
  TaskHandle_t task_command;
  TaskHandle_t task_telegram;
  TaskHandle_t task_app1;
  const char *app_name1;
  TaskHandle_t task_app2;
  const char *app_name2;
} tpl_tasks;

extern uint32_t cpu_load_core0;
extern uint32_t cpu_load_core1;

extern struct tpl_config_s {
  uint32_t bootCount;
  volatile bool allow_deepsleep;
  volatile bool ota_ongoing;
  volatile bool wifi_manager_shutdown_request;
  volatile bool wifi_manager_shutdown;
  volatile bool web_communication_ongoing;
  volatile bool ws_communication_ongoing;
  volatile bool bot_communication_ongoing;
#ifdef IS_ESP32CAM
  volatile bool ws_send_jpg_image;
  volatile bool bot_send_jpg_image;
#endif
#ifdef BOTtoken
  const char *receive_bot_token;  // do not change
  const char *send_bot_token;
  const char *bot_message;
  volatile bool bot_send_message;
#endif
  uint16_t reset_reason;
  const char *reset_reason_cpu0;
  const char *reset_reason_cpu1;
  uint32_t deepsleep_time_secs;  // =0 means off
  const char *stack_info;
  uint8_t *curr_jpg;
  size_t curr_jpg_len;
  uint32_t last_seen_watchpoint;
  uint32_t watchpoint;
} tpl_config;

#ifdef HAS_STEPPERS
#define TPL_STEPPER_FLAG 0x8000
#else
#define TPL_STEPPER_FLAG 0x0000
#endif
#define SPIFFS_CONFIG_VERSION (0x0101 | TPL_STEPPER_FLAG)
#define SPIFFS_CONFIG_FNAME "/config.ini"
class TplSpiffsConfig {
 public:
  uint16_t version;
  bool need_store;
#ifdef HAS_STEPPERS
  uint8_t nr_steppers;
#endif
  void init() {
    version = SPIFFS_CONFIG_VERSION;
#ifdef HAS_STEPPERS
    nr_steppers = 0;
#endif
    need_store = true;
  }
  TplSpiffsConfig() { init(); }
};
extern TplSpiffsConfig tpl_spiffs_config;

void tpl_update_stack_info();
void tpl_system_setup(uint32_t deep_sleep_secs = 0);
bool tpl_write_config();

struct rtc_data_s {
   uint16_t watchpoint;
   uint32_t bootCount;
};
extern struct rtc_data_s rtc_data;
#define WATCH(i)               \
  {                            \
    rtc_data.watchpoint = i;   \
    tpl_config.watchpoint = i; \
  }
