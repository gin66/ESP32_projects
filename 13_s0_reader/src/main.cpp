#include "base64.h"
#include "template.h"

using namespace std;

// can be used as parameter to tpl_command_setup
// void execute(enum Command command) {}

uint32_t w_last_second = 0;
uint32_t w_last_minute = 0;
uint32_t w_current = 0;
uint32_t cumulated_pulses = 0;

#define WATTS_ENTRIES (24 * 60)
uint32_t minute_data_read_idx = 0;
uint32_t minute_data_write_idx = 0;
struct minute_data {
  uint32_t watts_min;
  uint32_t watts_max;
  uint32_t pulses;
};
struct minute_data watts[WATTS_ENTRIES];
uint32_t request_idx = 1;

// can be used as parameter to tpl_websocket_setup
// void add_ws_info(DynamicJsonDocument* myObject) {}
void publish_func(DynamicJsonDocument *json) {
  (*json)["aport"] = analogRead(34);
  (*json)["dport"] = digitalRead(34);
  (*json)["cumulated_pulses"] = cumulated_pulses;
  (*json)["W"] = w_current;
  (*json)["read_idx"] = minute_data_read_idx;
  (*json)["write_idx"] = minute_data_write_idx;
  //(*json)["b64"] = base64::encode((uint8_t*)watts, WATTS_ENTRIES *
  //sizeof(struct minute_data));
  struct minute_data *x = &watts[request_idx % WATTS_ENTRIES];
  (*json)["idx"] = request_idx;
  (*json)["min_at_idx"] = x->watts_min;
  (*json)["max_at_idx"] = x->watts_max;
  (*json)["pulses_at_idx"] = x->pulses;
}

void process_func(DynamicJsonDocument *json) {
  if ((*json).containsKey("request_minute")) {
    request_idx = (*json)["request_minute"];
  }
}

//---------------------------------------------------
void setup() {
  tpl_system_setup(0);  // no deep sleep

  Serial.begin(115200);
  Serial.setDebugOutput(false);

  // Wait OTA
  tpl_wifi_setup(true, true, (gpio_num_t)255 /*tpl_ledPin*/);
  tpl_webserver_setup();
  tpl_websocket_setup(publish_func, process_func);
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
  pinMode(34, INPUT);
}

time_t last = 0;
uint32_t last_pulse_us = 0;
bool in_pulse = false;

void loop() {
  taskYIELD();
  // the S0 pulses are rather slow, so we just do polling
  bool process = false;
  if (analogRead(34) < 4000) {
    // pulse ongoing. process, if new
    process = !in_pulse;
    in_pulse = true;
  } else {
    in_pulse = false;
  }
  if (process) {
    cumulated_pulses++;

    time_t now = time(nullptr);
    uint32_t us = micros();
    uint32_t dt = us - last_pulse_us;
    last_pulse_us = us;
    // ensure cumulated_pulses > 5, so we are in process
    if ((dt < 10000000) && (dt > 20000) && (cumulated_pulses > 5)) {
      // 10 Imp/Wh => 1 Imp equals 0.1Wh
      // P = 0.1Wh * 1/(dt * 1us)
      // 1 ms = 1/(3600*1000000) h
      // P = 0.1W * 3600*1000000/dt
      //   = 360000000/dt
      w_current = 360000000 / dt;
    }

    if ((last / 60) != (now / 60)) {
      // new minute. advance write index
      if (minute_data_write_idx == request_idx) {
        request_idx++;
      }
      uint16_t r_idx = minute_data_read_idx % WATTS_ENTRIES;
      uint16_t w_idx = (++minute_data_write_idx) % WATTS_ENTRIES;

      // check if we overwrite last r_idx
      if (r_idx == w_idx) {
        r_idx++;
      }

      struct minute_data *x = &watts[w_idx];
      x->watts_min = w_current;
      x->watts_max = w_current;
      x->pulses = 0;
    }
    last = now;

    uint16_t w_idx = minute_data_write_idx % WATTS_ENTRIES;
    struct minute_data *x = &watts[w_idx];
    x->watts_min = min(x->watts_min, w_current);
    x->watts_max = max(x->watts_max, w_current);
    x->pulses++;
  }
}
