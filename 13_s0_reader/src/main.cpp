#include "template.h"

using namespace std;

// can be used as parameter to tpl_command_setup
// void execute(enum Command command) {}

uint32_t w_last_second = 0;
uint32_t w_last_minute = 0;
uint32_t w_last_hour = 0;
uint32_t w_current = 0;
uint32_t pulse = 0;
uint32_t pulseAtSecond[60] = {
	0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0
};
uint32_t pulseAtMinute[60] = {
	0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0
};
uint32_t pulseAtHour[24] = {
	0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,
	0,0,0,0
};

// can be used as parameter to tpl_websocket_setup
// void add_ws_info(DynamicJsonDocument* myObject) {}
void publish_func(DynamicJsonDocument * json) {
  (*json)["aport"] = analogRead(34);
  (*json)["dport"] = digitalRead(34);
  (*json)["pulse"] = pulse;
  (*json)["W_last_s"] = w_last_second;
  (*json)["W_last_m"] = w_last_minute;
  (*json)["W_last_h"] = w_last_hour;
  (*json)["W"] = w_current;
}

//---------------------------------------------------
void setup() {
  tpl_system_setup(0);  // no deep sleep

  Serial.begin(115200);
  Serial.setDebugOutput(false);

  // Wait OTA
  tpl_wifi_setup(true, true, (gpio_num_t)255 /*tpl_ledPin*/);
  tpl_webserver_setup();
  tpl_websocket_setup(publish_func, NULL);
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
uint32_t last_pulse_ms = 0;

void loop() {
  taskYIELD();
  // the S0 pulses are rather slow, so we just do polling
  if (analogRead(34) < 4000) {
	time_t now = time(nullptr);
    pulse++;
	uint32_t ms = millis();
	uint32_t dt = ms - last_pulse_ms;
	last_pulse_ms = ms;
	if ((dt < 10000) && (dt != 0)) {
		// 10 Imp/Wh => 1 Imp equals 0.1Wh
		// P = 0.1Wh * 1/(dt * 1ms)
		// 1 ms = 1/(3600*1000) h
		// P = 0.1W * 3600*1000/dt
		//   = 360000/dt
		w_current = 360000/dt;
	}

	uint8_t now_s = now % 60;
	uint8_t now_m = (now / 60) % 60;
	uint8_t now_h = (now / 3600) % 24;

    if (last != now) { // new second
	  // 10000 Imp/kWh = 10 Imp/Wh
	  // pulse / (10 Imp/Wh) yields Wh
	  w_last_second = pulseAtSecond[(now_s+59)%60] * 360; /// Wh * 3600 (sec/h) * 1/sec / 10
	  w_last_minute = pulseAtMinute[(now_m+59)%60] * 6; // Wh * 60 (min/h) * 1/min / 10
	  w_last_hour = pulseAtHour[(now_m+23)%24] / 10;

	  pulseAtSecond[now_s] = 0;
	  if (now_s == 0) {
	    pulseAtMinute[now_m] = 0;
	  }
	  if (now_s + now_m == 0) {
	    pulseAtHour[now_h] = 0;
	  }
	}
    last = now;

    pulseAtSecond[now_s]++;
    pulseAtMinute[now_m]++;
    pulseAtHour[now_h]++;

    const TickType_t xDelay = 3 / portTICK_PERIOD_MS;
    vTaskDelay(xDelay);
  }
//  Serial.print(digitalRead(34));
//  Serial.print(' ');
//  Serial.println(analogRead(34));
}
