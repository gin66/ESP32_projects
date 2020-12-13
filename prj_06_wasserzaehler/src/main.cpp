#include <Arduino.h>
#include <ArduinoOTA.h>
#include <esp_camera.h>

#include "string.h"
//#include <ArduinoJson.h>  // already included in UniversalTelegramBot.h
#include <ESP32Ping.h>
#include <ESPmDNS.h>
#include <UniversalTelegramBot.h>
#include <WebServer.h>
#include <WebSockets.h>
#include <WebSocketsClient.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <base64.h>
#include <mem.h>

#include "../../private_bot.h"
#include "esp32-hal-psram.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "fb_gfx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "img_converters.h"
#include "read.h"
#include "template.h"

#define DEBUG_ESP

#ifdef DEBUG_ESP
#define DBG(x) Serial.println(x)
#else
#define DBG(...)
#endif

#define ledPin 33
#define flashPin 4

using namespace std;

volatile bool status = false;
volatile bool deepsleep = true;

RTC_DATA_ATTR uint16_t bootCount = 0;

WebServer server(80);
extern const uint8_t index_html_start[] asm("_binary_src_index_html_start");
extern const uint8_t server_index_html_start[] asm(
    "_binary_src_serverindex_html_start");

WiFiClientSecure secured_client;
UniversalTelegramBot bot(wasserzaehler_BOTtoken, secured_client);

uint8_t* raw_image = NULL;
uint8_t digitized_image[WIDTH * HEIGHT / 8];
uint8_t filtered_image[WIDTH * HEIGHT / 8];
uint8_t temp_image[WIDTH * HEIGHT / 8];
struct read_s reader;

enum Command {
  IDLE,
  FLASH,
  MEASURE,
  DEEPSLEEP,
} command = MEASURE;  // make a photo and go to sleep

WebSocketsServer webSocket = WebSocketsServer(81);

bool init_camera() {
  uint8_t fail_cnt;
  char buf[40];
  esp_err_t err = tpl_init_camera(&fail_cnt, PIXFORMAT_JPEG, FRAMESIZE_QVGA);
  if (err == ESP_OK) {
    if (fail_cnt > 0) {
      sprintf(buf, "Camera init succeeded with %d fails", fail_cnt);
      bot.sendMessage(CHAT_ID, buf);
    }
    ESP_LOGE(TAG, "Total heap: %u", ESP.getHeapSize());
    ESP_LOGE(TAG, "Free heap: %u", ESP.getFreeHeap());
    ESP_LOGE(TAG, "Total PSRAM: %u", ESP.getPsramSize());
    ESP_LOGE(TAG, "Free PSRAM: %u", ESP.getFreePsram());
    sensor_t* sensor = esp_camera_sensor_get();
    sensor->set_brightness(sensor, -2);
    sensor->set_contrast(sensor, -2);
    sensor->set_saturation(sensor, 2);
    sensor->set_raw_gma(sensor, 1);
    sensor->set_dcw(sensor, 0);
    sensor->set_hmirror(sensor, 0);
    sensor->set_vflip(sensor, 0);
    return true;
  }
  // cannot init camera
  sprintf(buf, "Camera init failed for 20 times: %x", err);
  bot.sendMessage(CHAT_ID, buf);
  return false;
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload,
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
      Serial.println((char*)payload);
      DynamicJsonDocument json(4096);
      deserializeJson(json, (char*)payload);
      if (json.containsKey("flash")) {
        command = FLASH;
      }
      if (json.containsKey("brightness")) {
        int8_t brightness = json["brightness"];
        sensor_t* sensor = esp_camera_sensor_get();
        sensor->set_brightness(sensor, brightness);
      }
      if (json.containsKey("contrast")) {
        int8_t contrast = json["contrast"];
        sensor_t* sensor = esp_camera_sensor_get();
        sensor->set_contrast(sensor, contrast);
      }
      if (json.containsKey("saturation")) {
        int8_t saturation = json["saturation"];
        sensor_t* sensor = esp_camera_sensor_get();
        sensor->set_saturation(sensor, saturation);
      }
      if (json.containsKey("ae_level")) {
        int8_t ae_level = json["ae_level"];
        sensor_t* sensor = esp_camera_sensor_get();
        sensor->set_ae_level(sensor, ae_level);
      }
      if (json.containsKey("set_aec_value")) {
        int8_t set_aec_value = json["set_aec_value"];
        sensor_t* sensor = esp_camera_sensor_get();
        sensor->set_aec_value(sensor, set_aec_value);
      }
      if (json.containsKey("gain_ctrl")) {
        int8_t gain_ctrl = json["gain_ctrl"];
        sensor_t* sensor = esp_camera_sensor_get();
        sensor->set_gain_ctrl(sensor, gain_ctrl);
      }
      if (json.containsKey("gainceiling")) {
        int8_t gainceiling = json["gainceiling"];
        sensor_t* sensor = esp_camera_sensor_get();
        sensor->set_gainceiling(sensor, (gainceiling_t)gainceiling);
      }
      if (json.containsKey("bpc")) {
        bool v = json["bpc"];
        sensor_t* sensor = esp_camera_sensor_get();
        sensor->set_bpc(sensor, v ? 1 : 0);
      }
      if (json.containsKey("whitebal")) {
        bool v = json["whitebal"];
        sensor_t* sensor = esp_camera_sensor_get();
        sensor->set_whitebal(sensor, v ? 1 : 0);
      }
      if (json.containsKey("exposure_ctrl")) {
        bool v = json["exposure_ctrl"];
        sensor_t* sensor = esp_camera_sensor_get();
        sensor->set_exposure_ctrl(sensor, v ? 1 : 0);
      }
      if (json.containsKey("awb_gain")) {
        bool v = json["awb_gain"];
        sensor_t* sensor = esp_camera_sensor_get();
        sensor->set_awb_gain(sensor, v ? 1 : 0);
      }
      if (json.containsKey("wpc")) {
        bool v = json["wpc"];
        sensor_t* sensor = esp_camera_sensor_get();
        sensor->set_wpc(sensor, v ? 1 : 0);
      }
      if (json.containsKey("dcw")) {
        bool v = json["dcw"];
        sensor_t* sensor = esp_camera_sensor_get();
        sensor->set_dcw(sensor, v ? 1 : 0);
      }
      if (json.containsKey("raw_gma")) {
        bool v = json["raw_gma"];
        sensor_t* sensor = esp_camera_sensor_get();
        sensor->set_raw_gma(sensor, v ? 1 : 0);
      }
      if (json.containsKey("vflip")) {
        bool v = json["vflip"];
        sensor_t* sensor = esp_camera_sensor_get();
        sensor->set_vflip(sensor, v ? 1 : 0);
      }
      if (json.containsKey("hmirror")) {
        bool v = json["hmirror"];
        sensor_t* sensor = esp_camera_sensor_get();
        sensor->set_hmirror(sensor, v ? 1 : 0);
      }
      if (json.containsKey("image")) {
        if (init_camera()) {
          camera_fb_t* fb = esp_camera_fb_get();
          if (!fb) {
            Serial.println("Camera capture failed");
            delay(1000);
            ESP.restart();
          } else {
            status = true;
            bool raw = json["image"];
            if ((fb->format == PIXFORMAT_JPEG) && raw) {
              webSocket.broadcastBIN(fb->buf, fb->len);
              esp_camera_fb_return(fb);
            } else if ((fb->format == PIXFORMAT_JPEG) && !raw) {
              if (raw_image == NULL) {
                raw_image = (uint8_t*)ps_malloc(WIDTH * HEIGHT * 3);
              }
              fmt2rgb888(fb->buf, fb->len, fb->format, raw_image);
              uint8_t head[5];
              head[0] = 3;
              head[1] = fb->width >> 8;
              head[2] = fb->width & 0xff;
              head[3] = fb->height >> 8;
              head[4] = fb->height & 0xff;
              esp_camera_fb_return(fb);
              digitize(raw_image, digitized_image, &reader);
              find_pointer(digitized_image, filtered_image, temp_image,
                           &reader);
              webSocket.broadcastBIN(head, 5);
              webSocket.broadcastBIN(filtered_image, WIDTH * HEIGHT / 8);
            } else {
              esp_camera_fb_return(fb);
            }
          }
        }
      }
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

void TaskWebSocket(void* pvParameters) {
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
      myObject["PSmem_free"] = (long)ESP.getFreePsram();
      myObject["stack_free"] = (long)uxTaskGetStackHighWaterMark(NULL);
      // myObject["time"] = formattedTime;
      // myObject["b64"] = base64::encode((uint8_t*)data_buf, data_idx);
      // myObject["button_analog"] = analogRead(BUTTON_PIN);
      // myObject["button_digital"] = digitalRead(BUTTON_PIN);
      myObject["flash"] = digitalRead(flashPin);
      myObject["digital"] = data;
      // myObject["sample_rate"] = I2S_SAMPLE_RATE;
      myObject["wifi_dBm"] = WiFi.RSSI();
      myObject["SSID"] = WiFi.SSID();
      myObject["IP"] = WiFi.localIP().toString();

      myObject["status"] = status;
      myObject["bootCount"] = bootCount;

      if (reader.candidates == 4) {
        myObject["a1"] = reader.pointer[0].angle;
        myObject["a2"] = reader.pointer[1].angle;
        myObject["a3"] = reader.pointer[2].angle;
        myObject["a4"] = reader.pointer[3].angle;
        myObject["x1"] = reader.pointer[0].col_center2/2;
        myObject["x2"] = reader.pointer[1].col_center2/2;
        myObject["x3"] = reader.pointer[2].col_center2/2;
        myObject["x4"] = reader.pointer[3].col_center2/2;
        myObject["y1"] = reader.pointer[0].row_center2/2;
        myObject["y2"] = reader.pointer[1].row_center2/2;
        myObject["y3"] = reader.pointer[2].row_center2/2;
        myObject["y4"] = reader.pointer[3].row_center2/2;
      }
#define BUFLEN 4096
      char buffer[BUFLEN];
      /* size_t bx = */ serializeJson(myObject, &buffer, BUFLEN);
      String as_json = String(buffer);
      webSocket.broadcastTXT(as_json);
    }
    vTaskDelay(xDelay);
  }
}

void handleNewMessages(int numNewMessages) {
  Serial.println("handleNewMessages");
  Serial.println(String(numNewMessages));

  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = bot.messages[i].chat_id;
    String text = bot.messages[i].text;

    String from_name = bot.messages[i].from_name;
    if (from_name == "") from_name = "Guest";

    if (text == "/test") {
      bot.sendChatAction(chat_id, "typing");
      delay(4000);
      bot.sendMessage(chat_id, "Did you see the action message?");
    }

    if (text == "/nosleep") {
      deepsleep = false;
      bot.sendMessage(chat_id, "Prevent sleep");
    }
    if (text == "/allowsleep") {
      deepsleep = true;
      bot.sendMessage(chat_id, "Allow sleep");
    }

    if (text == "/start") {
      String welcome = "Welcome to Universal Arduino Telegram Bot library, " +
                       from_name + ".\n";
      welcome += "This is Chat Action Bot example.\n\n";
      welcome += "/test : Show typing, then reply\n";
      welcome += "/nosleep : Prevent deep sleep\n";
      welcome += "/allowsleep : Allow deep sleep\n";
      bot.sendMessage(chat_id, welcome);
    }
  }
}
//---------------------------------------------------
void setup() {
  bootCount++;

  // turn flash light off
  digitalWrite(flashPin, LOW);
  pinMode(flashPin, OUTPUT);

  // ensure camera module not powered up
  digitalWrite(PWDN_GPIO_NUM, HIGH);
  pinMode(PWDN_GPIO_NUM, OUTPUT);

#ifdef DEBUG_ESP
  Serial.begin(115200);
  Serial.setDebugOutput(true);
#endif
  esp_log_level_set("*", ESP_LOG_VERBOSE);
  ESP_LOGE(TAG, "Free heap: %u", xPortGetFreeHeapSize());

  my_wifi_setup(true);

  // Add root certificate for api.telegram.org
  secured_client.setCACert(TELEGRAM_CERTIFICATE_ROOT);

  server.onNotFound([]() {
    server.send(404);
    Serial.print("Not found: ");
    Serial.println(server.uri());
  });
  /*handling uploading firmware file */
  server.on("/", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send_P(200, "text/html", (const char*)index_html_start);
  });
  server.on("/serverIndex", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send_P(200, "text/html", (const char*)server_index_html_start);
  });
  server.on("/image", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    if (init_camera()) {
      camera_fb_t* fb = esp_camera_fb_get();
      if (fb) {
        if (fb->format == PIXFORMAT_RGB565) {
          server.send_P(200, "application/octet-stream", (const char*)fb->buf,
                        fb->len);
        } else if (fb->format == PIXFORMAT_JPEG) {
          server.send_P(200, "image/jpeg", (const char*)fb->buf, fb->len);
        }
        esp_camera_fb_return(fb);
      }
    }
  });
  /*handling uploading firmware file */
  server.on(
      "/update", HTTP_POST,
      []() {
        server.sendHeader("Connection", "close");
        server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
        ESP.restart();
      },
      []() {
        HTTPUpload& upload = server.upload();
        if (upload.status == UPLOAD_FILE_START) {
          Serial.printf("Update: %s\n", upload.filename.c_str());
          if (!Update.begin(
                  UPDATE_SIZE_UNKNOWN)) {  // start with max available size
            Update.printError(Serial);
          }
        } else if (upload.status == UPLOAD_FILE_WRITE) {
          /* flashing firmware to ESP*/
          if (Update.write(upload.buf, upload.currentSize) !=
              upload.currentSize) {
            Update.printError(Serial);
          }
        } else if (upload.status == UPLOAD_FILE_END) {
          if (Update.end(
                  true)) {  // true to set the size to the current progress
            Serial.printf("Update Success: %u\nRebooting...\n",
                          upload.totalSize);
          } else {
            Update.printError(Serial);
          }
        }
      });
  server.begin();

  if (psramFound()) {
    Serial.println("PSRAM found and loaded");
  }
  ESP_LOGE(TAG, "Free heap after setup: %u", xPortGetFreeHeapSize());
  ESP_LOGE(TAG, "Total heap: %u", ESP.getHeapSize());
  ESP_LOGE(TAG, "Free heap: %u", ESP.getFreeHeap());
  ESP_LOGE(TAG, "Total PSRAM: %u", ESP.getPsramSize());
  ESP_LOGE(TAG, "Free PSRAM: %u", ESP.getFreePsram());

  BaseType_t rc;

  rc = xTaskCreatePinnedToCore(TaskWebSocket, "WebSocket", 16384, (void*)1, 1,
                               NULL, 0);
  if (rc != pdPASS) {
    Serial.print("cannot start websocket task=");
    Serial.println(rc);
  }

  startNetWatchDog();

  if (MDNS.begin(HOSTNAME)) {
    Serial.println("MDNS responder started");
  }
  MDNS.addService("http", "tcp", 80);
  MDNS.addService("ws", "tcp", 81);

  Serial.print("Retrieving time: ");
  configTime(0, 0, "pool.ntp.org");  // get UTC time via NTP
  time_t now = time(nullptr);
  while (now < 24 * 3600) {
    Serial.print(".");
    delay(100);
    now = time(nullptr);
  }

  Serial.println("Setup done.");
}

camera_fb_t* photo_fb = NULL;
uint32_t dataBytesSent;
bool isMoreDataAvailable() {
  if (photo_fb) {
    return (dataBytesSent < photo_fb->len);
  } else {
    return false;
  }
}
byte* getNextBuffer() {
  if (photo_fb) {
    byte* buf = &photo_fb->buf[dataBytesSent];
    dataBytesSent += 1024;
    return buf;
  } else {
    return nullptr;
  }
}

int getNextBufferLen() {
  if (photo_fb) {
    uint32_t rem = photo_fb->len - dataBytesSent;
    if (rem > 1024) {
      return 1024;
    } else {
      return rem;
    }
  } else {
    return 0;
  }
}

const unsigned long BOT_MTBS = 1000;  // mean time between scan messages
unsigned long bot_lasttime = 0;       // last time messages' scan has been done
void loop() {
  my_wifi_loop(true);

  uint32_t period = 1000;
  if (fail >= 50) {
    period = 300;
  }
  if ((millis() / period) & 1) {
    pinMode(ledPin, OUTPUT);
    digitalWrite(ledPin, HIGH);
  } else {
    pinMode(ledPin, OUTPUT);
    digitalWrite(ledPin, LOW);
  }
  server.handleClient();

  if (millis() >= bot_lasttime) {
    // Perhaps reason for 50kBytes mem_free negative spikes
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

    while (numNewMessages) {
      command = IDLE;
      Serial.println("got response");
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }

    bot_lasttime = millis() + BOT_MTBS;
  }

  switch (command) {
    case IDLE:
      break;
	case MEASURE:
      status = bot.sendMessage(CHAT_ID, "Camera capture");
      if (init_camera()) {
        digitalWrite(flashPin, HIGH);
        for (uint8_t i = 0; i < 10; i++) {
          // let the camera adjust
          photo_fb = esp_camera_fb_get();
          esp_camera_fb_return(photo_fb);
          photo_fb = NULL;
        }
        photo_fb = esp_camera_fb_get();
        if (!photo_fb) {
          status = bot.sendMessage(CHAT_ID, "Camera capture failed");
        } else {
          dataBytesSent = 0;
          status = bot.sendPhotoByBinary(CHAT_ID, "image/jpeg", photo_fb->len,
                                         isMoreDataAvailable, nullptr,
                                         getNextBuffer, getNextBufferLen);
          esp_camera_fb_return(photo_fb);
          photo_fb = NULL;
          bot.sendMessage(
              CHAT_ID, WiFi.SSID() + String(": ") + WiFi.localIP().toString());
        }
        for (uint8_t i = 0; i < 10; i++) {
              photo_fb = esp_camera_fb_get();
              if (raw_image == NULL) {
                raw_image = (uint8_t*)ps_malloc(WIDTH * HEIGHT * 3);
              }
              fmt2rgb888(photo_fb->buf, photo_fb->len, photo_fb->format, raw_image);
          esp_camera_fb_return(photo_fb);
          photo_fb = NULL;
              digitize(raw_image, digitized_image, &reader);
              find_pointer(digitized_image, filtered_image, temp_image,
                           &reader);
			  if (reader.candidates == 4) {
          bot.sendMessage(
              CHAT_ID, String("Result: ")
			  + reader.pointer[0].angle + String("/")
			  + reader.pointer[1].angle + String("/")
			  + reader.pointer[2].angle + String("/")
			  + reader.pointer[3].angle
			  );
				  break;
			  }
	    }
        digitalWrite(flashPin, LOW);
      }
      command = DEEPSLEEP;
	  break;
    case FLASH:
      digitalWrite(flashPin, !digitalRead(flashPin));
      command = IDLE;
      break;
    case DEEPSLEEP:
      if (deepsleep) {
        // ensure LEDs and camera module off and pull up/downs set
        // appropriately
        digitalWrite(ledPin, HIGH);
        digitalWrite(flashPin, LOW);
        pinMode(ledPin, INPUT_PULLUP);
        pinMode(flashPin, INPUT_PULLDOWN);
        tpl_camera_off();
        // wake up every ten minutes
        esp_sleep_enable_timer_wakeup(600LL * 1000000LL);
        esp_deep_sleep_start();
      }
      command = IDLE;
      break;
  }

  if (millis() > 1000L * 60L * 10L) {
    // reset every 10 minutes
    esp_sleep_enable_timer_wakeup(1LL * 1000000LL);
    esp_deep_sleep_start();
  }
}
