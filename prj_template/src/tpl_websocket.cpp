#include "tpl_websocket.h"

#include "tpl_command.h"
#include "tpl_system.h"
#ifdef IS_ESP32CAM
#include "tpl_esp_camera.h"
#endif

WebSocketsServer webSocket = WebSocketsServer(81);

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
#ifdef IS_ESP32CAM
      tpl_process_web_socket_cam_settings(&json);
      if (json.containsKey("image")) {
        tpl_command = CmdSendJpg;
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
  void (*func)(DynamicJsonDocument * json) =
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
      String data = "........................................";
      for (int i = 0; i < 40; i++) {
        char ch = 48 + digitalRead(i);
        data.setCharAt(i, ch);
      }
      DynamicJsonDocument myObject(4096);
      myObject["millis"] = millis();
      myObject["mem_free"] = (long)ESP.getFreeHeap();
      myObject["stack_free"] = (long)uxTaskGetStackHighWaterMark(NULL);
      myObject["reset_reason"] = tpl_config.reset_reason;
      // myObject["time"] = formattedTime;
      // myObject["b64"] = base64::encode((uint8_t*)data_buf, data_idx);
      // myObject["button_analog"] = analogRead(BUTTON_PIN);
      // myObject["button_digital"] = digitalRead(BUTTON_PIN);
      myObject["digital"] = data;
      // myObject["sample_rate"] = I2S_SAMPLE_RATE;
      myObject["wifi_dBm"] = WiFi.RSSI();
      myObject["IP"] = WiFi.localIP().toString();
      myObject["SSID"] = WiFi.SSID();

      if (func != NULL) {
        func(&myObject);
      }

      /* size_t bx = */ serializeJson(myObject, &ws_buffer, WS_BUFLEN);
      String as_json = String(ws_buffer);
      webSocket.broadcastTXT(as_json);
    }
#ifdef IS_ESP32CAM
    if (tpl_config.ws_send_jpg_image) {
      camera_fb_t *fb = esp_camera_fb_get();
      webSocket.broadcastBIN(fb->buf, fb->len);
      esp_camera_fb_return(fb);
      tpl_config.ws_send_jpg_image = false;
    }
#endif
    vTaskDelay(xDelay);
  }
}

//---------------------------------------------------
//
void tpl_websocket_setup(void (*func)(DynamicJsonDocument *json)) {
  xTaskCreatePinnedToCore(TaskWebSocketCore0, "WebSocket", 4096, (void *)func,
                          1, &tpl_tasks.task_websocket, CORE_0);
}
