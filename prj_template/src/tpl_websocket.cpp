#include "tpl_websocket.h"

#include "tpl_command.h"
#include "tpl_system.h"
#ifdef IS_ESP32CAM
#include "tpl_esp_camera.h"
#endif

WebSocketsServer webSocket = WebSocketsServer(81);
void (*tpl_process_func)(DynamicJsonDocument *json) = NULL;

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload,
                    size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.print("websocket disconnected: ");
      Serial.println(num);
      break;
    case WStype_CONNECTED:
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
static DynamicJsonDocument *ws_json = NULL;

void TaskWebSocketCore0(void *pvParameters) {
  void (*publish_func)(DynamicJsonDocument *json) =
      (void (*)(DynamicJsonDocument *))pvParameters;
  const TickType_t xDelay = 1 + 10 / portTICK_PERIOD_MS;
  uint32_t send_status_ms = 0;

  ws_json = new DynamicJsonDocument(4096);
  if (!ws_json) {
    Serial.println("Failed to allocate WebSocket JSON buffer");
    vTaskDelete(NULL);
    return;
  }

  Serial.println("WebSocket Task started");
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  for (;;) {
    uint32_t now = millis();
    webSocket.loop();

    if (now > send_status_ms) {
      send_status_ms = now + 750;
      int clients = webSocket.connectedClients();
      if (clients > 0) {
        //Serial.print("Websockets=");
        //Serial.println(clients);
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
        ws_json->clear();
        (*ws_json)["millis"] = millis();
        (*ws_json)["mem_free"] = (long)ESP.getFreeHeap();
#ifdef IS_ESP32CAM
        (*ws_json)["ps_free"] = (long)ESP.getFreePsram();
#endif
        (*ws_json)["stack_free"] = (long)uxTaskGetStackHighWaterMark(NULL);
        (*ws_json)["reset_cpu0"] = tpl_config.reset_reason_cpu0;
        (*ws_json)["reset_cpu1"] = tpl_config.reset_reason_cpu1;
        (*ws_json)["watch"] = tpl_config.watchpoint;
        (*ws_json)["last_watch"] = tpl_config.last_seen_watchpoint;
        (*ws_json)["bootCount"] = tpl_config.bootCount;
        // (*ws_json)["time"] = formattedTime;
        // (*ws_json)["b64"] = base64::encode((uint8_t*)data_buf, data_idx);
        // (*ws_json)["button_analog"] = analogRead(BUTTON_PIN);
        // (*ws_json)["button_digital"] = digitalRead(BUTTON_PIN);
        (*ws_json)["digital"] = data;
        // (*ws_json)["sample_rate"] = I2S_SAMPLE_RATE;
        (*ws_json)["wifi_dBm"] = WiFi.RSSI();
        (*ws_json)["IP"] = WiFi.localIP().toString();
        (*ws_json)["SSID"] = WiFi.SSID();
#ifdef HAS_STEPPERS
        (*ws_json)["nr_stepper"] = tpl_spiffs_config.nr_steppers;
#endif
        (*ws_json)["dirty_config"] = tpl_spiffs_config.need_store;
        (*ws_json)["cpu_load_core0"] = cpu_load_core0;
        (*ws_json)["cpu_load_core1"] = cpu_load_core1;

        if (publish_func != NULL) {
          publish_func(ws_json);
        }

        serializeJson(*ws_json, ws_buffer, WS_BUFLEN);
        webSocket.broadcastTXT(ws_buffer, strlen(ws_buffer));
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
