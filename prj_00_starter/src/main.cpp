#include "template.h"

using namespace std;

// can be used as parameter to tpl_command_setup
// void execute(enum Command command) {}

// can be used as parameter to tpl_websocket_setup
// void add_ws_info(DynamicJsonDocument* myObject) {}

//---------------------------------------------------
void setup() {
  tpl_system_setup(0);  // no deep sleep

  Serial.begin(115200);
  Serial.setDebugOutput(false);

  // Wait OTA
  tpl_wifi_setup(true, true, (gpio_num_t)tpl_ledPin);
  tpl_webserver_setup();
  tpl_websocket_setup(NULL);
  tpl_net_watchdog_setup();
  tpl_command_setup(NULL);

#ifdef IS_ESP32CAM
  uint8_t fail_cnt = 0;
#ifdef BOTtoken
  tpl_camera_setup(&fail_cnt, FRAMESIZE_QVGA);
#else
  tpl_camera_setup(&fail_cnt, FRAMESIZE_VGA);
#endif
#endif

#ifdef BOTtoken
  tpl_telegram_setup(BOTtoken, CHAT_ID);
#endif

  Serial.println("Setup done.");
}

void loop() {
  const TickType_t xDelay = 10 / portTICK_PERIOD_MS;
  vTaskDelay(xDelay);
}
