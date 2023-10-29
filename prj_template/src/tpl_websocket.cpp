#include "tpl_websocket.h"

#include "tpl_command.h"
#include "tpl_system.h"
#ifdef IS_ESP32CAM
#include "tpl_esp_camera.h"
#endif

WebSocketsServer webSocket = WebSocketsServer(81);
void (*tpl_process_func)(DynamicJsonDocument *json) = NULL;
uint8_t websocket_connections = 0;

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload,
                    size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      if (websocket_connections > 0) {
         websocket_connections--;
      }
      Serial.print("websocket disconnected: ");
      Serial.println(num);
      break;
    case WStype_CONNECTED:
      websocket_connections++;
      Serial.print("websocket connected: ");
      Serial.println(num);
      break;
    case WStype_TEXT: {
      Serial.print("received from websocket: ");
      Serial.println((char *)payload);
      DynamicJsonDocument json(4096);
      deserializeJson(json, (char *)payload);
      if (json.containsKey("sleep")) {
        tpl_config.allow_deepsleep = json["sleep"];
      }
      if (json.containsKey("deepsleep")) {
        tpl_config.allow_deepsleep = true;
        tpl_command = CmdDeepSleep;
      }
      if (json.containsKey("save_config")) {
        tpl_command = CmdSaveConfig;
      }
      if (json.containsKey("pin_to_high")) {
        uint8_t pin = json["pin_to_high"];
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);
      }
      if (json.containsKey("pin_to_low")) {
        uint8_t pin = json["pin_to_low"];
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
      }
      if (json.containsKey("as_input")) {
        uint8_t pin = json["as_input"];
        pinMode(pin, INPUT);
      }
      if (json.containsKey("as_input_pullup")) {
        uint8_t pin = json["as_input_pullup"];
        pinMode(pin, INPUT_PULLUP);
      }
      if (json.containsKey("as_input_pulldown")) {
        uint8_t pin = json["as_input_pulldown"];
        pinMode(pin, INPUT_PULLDOWN);
      }
      if (tpl_process_func) {
        tpl_process_func(&json);
      }
#ifdef IS_ESP32CAM
      tpl_process_web_socket_cam_settings(&json);
      if (json.containsKey("image")) {
        tpl_command = CmdSendJpg2Ws;
      }
      if (json.containsKey("flash")) {
        tpl_command = CmdFlash;
      }
#endif
    } break;
    case WStype_BIN:
    case WStype_ERROR:
    case WStype_FRAGMENT:
    case WStype_FRAGMENT_FIN:
    case WStype_FRAGMENT_TEXT_START:
    case WStype_FRAGMENT_BIN_START:
    case WStype_PING:
    case WStype_PONG:
      break;
  }
}

#define WS_BUFLEN 4096
char ws_buffer[WS_BUFLEN];
void TaskWebSocketCore0(void *pvParameters) {
  void (*publish_func)(DynamicJsonDocument *json) =
      (void (*)(DynamicJsonDocument *))pvParameters;
  const TickType_t xDelay = 1 + 10 / portTICK_PERIOD_MS;
  uint32_t send_status_ms = 0;

  Serial.println("WebSocket Task started");
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  for (;;) {
    uint32_t now = millis();
    webSocket.loop();

    if (now > send_status_ms) {
      send_status_ms = now + 100;
      if (websocket_connections > 0) {
      String data = "................................................";
      for (int i = 0; i < 48; i++) {
        char ch = '-';
        volatile uint32_t *reg = portInputRegister(digitalPinToPort(i));
        volatile uint32_t *ddr = portModeRegister(digitalPinToPort(i));
        if (reg) {
          uint32_t mask = digitalPinToBitMask(i);
          if (ddr && (*ddr & mask)) {
            ch = *reg & mask ? '1' : '0';
          } else {
            ch = *reg & mask ? 'H' : 'L';
          }
        }
        data.setCharAt(i, ch);
      }
      DynamicJsonDocument myObject(4096);
      myObject["millis"] = millis();
      myObject["mem_free"] = (long)ESP.getFreeHeap();
#ifdef IS_ESP32CAM
      myObject["ps_free"] = (long)ESP.getFreePsram();
#endif
      myObject["stack_free"] = (long)uxTaskGetStackHighWaterMark(NULL);
      myObject["reset_cpu0"] = tpl_config.reset_reason_cpu0;
      myObject["reset_cpu1"] = tpl_config.reset_reason_cpu1;
      myObject["watch"] = tpl_config.watchpoint;
      myObject["last_watch"] = tpl_config.last_seen_watchpoint;
      // myObject["time"] = formattedTime;
      // myObject["b64"] = base64::encode((uint8_t*)data_buf, data_idx);
      // myObject["button_analog"] = analogRead(BUTTON_PIN);
      // myObject["button_digital"] = digitalRead(BUTTON_PIN);
      myObject["digital"] = data;
      // myObject["sample_rate"] = I2S_SAMPLE_RATE;
      myObject["wifi_dBm"] = WiFi.RSSI();
      myObject["IP"] = WiFi.localIP().toString();
      myObject["SSID"] = WiFi.SSID();
#ifdef HAS_STEPPERS
      myObject["nr_stepper"] = tpl_spiffs_config.nr_steppers;
#endif
      myObject["dirty_config"] = tpl_spiffs_config.need_store;

      if (publish_func != NULL) {
        publish_func(&myObject);
      }

      /* size_t bx = */ serializeJson(myObject, &ws_buffer, WS_BUFLEN);
      String as_json = String(ws_buffer);
      webSocket.broadcastTXT(as_json);
    }
    }
#ifdef IS_ESP32CAM
    if (tpl_config.ws_send_jpg_image) {
      camera_fb_t *fb = esp_camera_fb_get();
      if (fb) {
        webSocket.broadcastBIN(fb->buf, fb->len);
        esp_camera_fb_return(fb);
      }
      tpl_config.ws_send_jpg_image = false;
    }
#endif
    vTaskDelay(xDelay);
  }
}

//---------------------------------------------------
//
void tpl_websocket_setup(void (*publish_func)(DynamicJsonDocument *json),
                         void (*process_json)(DynamicJsonDocument *json)) {
  tpl_process_func = process_json;
  xTaskCreatePinnedToCore(TaskWebSocketCore0, "WebSocket", 4096,
                          (void *)publish_func, 1, &tpl_tasks.task_websocket,
                          CORE_0);
}
