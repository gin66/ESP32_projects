#include <esp_camera.h>

#include "string.h"
//#include "soc/soc.h"
//#include "soc/rtc_cntl_reg.h"

#include <Arduino.h>
#include <ArduinoOTA.h>
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
#include "qrcode_recognize.h"
#include "template.h"

#define CONFIG_CAMERA_MODEL_AI_THINKER 1
#include "app_camera.h"
#include "quirc.h"

#define DEBUG_ESP

#ifdef DEBUG_ESP
#define DBG(x) Serial.println(x)
#else
#define DBG(...)
#endif

#define ledPin 33
#define flashPin 4

using namespace std;

volatile bool camera_in_use = false;
volatile bool status = false;
volatile bool deepsleep = true;

RTC_DATA_ATTR uint16_t bootCount = 0;

WebServer server(80);
extern const uint8_t index_html_start[] asm("_binary_src_index_html_start");
extern const uint8_t server_index_html_start[] asm(
    "_binary_src_serverindex_html_start");

WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOTtoken, secured_client);

bool qrMode = true;

enum Command {
  IDLE,
  HEIZUNG,
  DEEPSLEEP,
} command = HEIZUNG;  // make a photo and go to sleep

#define MAX_CODES 4
struct quirc_code qr_codes[MAX_CODES];
int qr_identify(camera_fb_t* fb) {
  int id_count = 0;
  struct quirc* qr_recognizer = quirc_new();
  if (!qr_recognizer) {
    ESP_LOGE(TAG, "Can't create quirc object");
  } else {
    if (quirc_resize(qr_recognizer, fb->width, fb->height) < 0) {
      ESP_LOGE(TAG,
               "Resize the QR-code recognizer err (cannot allocate memory).");
    } else {
      int w, h;
      uint8_t* image = quirc_begin(qr_recognizer, &w, &h);
      if (image) {
        memcpy(image, fb->buf, w * h);
        quirc_end(qr_recognizer);

        // Return the number of QR-codes identified in the last processed
        // image.
        id_count = quirc_count(qr_recognizer);
        ESP_LOGE(TAG, "qr_codes %d", id_count);
        if (id_count > MAX_CODES) {
          id_count = MAX_CODES;
        }
        for (int i = 0; i < id_count; i++) {
          quirc_extract(qr_recognizer, i, &qr_codes[i]);
          ESP_LOGE(TAG, "%d/%d %d/%d %d/%d %d/%d", qr_codes[i].corners[0].x,
                   qr_codes[i].corners[0].y, qr_codes[i].corners[1].x,
                   qr_codes[i].corners[1].y, qr_codes[i].corners[2].x,
                   qr_codes[i].corners[2].y, qr_codes[i].corners[3].x,
                   qr_codes[i].corners[3].y);
        }
      } else {
        ESP_LOGE(TAG, "no image, dimension %d*%d != %d, %d*%d", w, h, fb->len,
                 fb->width, fb->height);
      }
    }
    ESP_LOGE(TAG, "Deconstruct QR-Code recognizer(quirc)");
    quirc_destroy(qr_recognizer);
  }
  return id_count;
}

WebSocketsServer webSocket = WebSocketsServer(81);

bool init_camera() {
  if (camera_in_use) {
    return true;
  }
  // try camera init for max 20 times
  for (uint8_t i = 0; i < 20; i++) {
    camera_config_t camera_config;

    // Turn camera power on and wait 100ms
    digitalWrite(PWDN_GPIO_NUM, LOW);
    pinMode(PWDN_GPIO_NUM, OUTPUT);
    delay(100);

    camera_config.ledc_channel = LEDC_CHANNEL_0;
    camera_config.ledc_timer = LEDC_TIMER_0;
    camera_config.pin_d0 = Y2_GPIO_NUM;
    camera_config.pin_d1 = Y3_GPIO_NUM;
    camera_config.pin_d2 = Y4_GPIO_NUM;
    camera_config.pin_d3 = Y5_GPIO_NUM;
    camera_config.pin_d4 = Y6_GPIO_NUM;
    camera_config.pin_d5 = Y7_GPIO_NUM;
    camera_config.pin_d6 = Y8_GPIO_NUM;
    camera_config.pin_d7 = Y9_GPIO_NUM;
    camera_config.pin_xclk = XCLK_GPIO_NUM;
    camera_config.pin_pclk = PCLK_GPIO_NUM;
    camera_config.pin_vsync = VSYNC_GPIO_NUM;
    camera_config.pin_href = HREF_GPIO_NUM;
    camera_config.pin_sscb_sda = SIOD_GPIO_NUM;
    camera_config.pin_sscb_scl = SIOC_GPIO_NUM;
    // Do not let the driver to operate PWDN_GPIO_NUM;
    camera_config.pin_pwdn = -1;  // PWDN_GPIO_NUM;
    camera_config.pin_reset = RESET_GPIO_NUM;
    camera_config.xclk_freq_hz = 10000000;
    if (false) {
      // higher resolution leads to artefacts
      camera_config.pixel_format = PIXFORMAT_GRAYSCALE;
      camera_config.frame_size = FRAMESIZE_QVGA;
    } else {
      camera_config.pixel_format = PIXFORMAT_JPEG;
      camera_config.frame_size = FRAMESIZE_VGA;
    }

    // quality of JPEG output. 0-63 lower means higher quality
    camera_config.jpeg_quality = 5;
    // 1: Wait for V-Synch // 2: Continous Capture (Video)
    camera_config.fb_count = 1;
    esp_err_t err = esp_camera_init(&camera_config);
    if (err == ESP_OK) {
      if (i > 0) {
        char buf[40];
        sprintf(buf, "Camera init succeeded with %d fails", i);
        bot.sendMessage(CHAT_ID, buf);
      }
      ESP_LOGE(TAG, "Total heap: %u", ESP.getHeapSize());
      ESP_LOGE(TAG, "Free heap: %u", ESP.getFreeHeap());
      ESP_LOGE(TAG, "Total PSRAM: %u", ESP.getPsramSize());
      ESP_LOGE(TAG, "Free PSRAM: %u", ESP.getFreePsram());
      sensor_t* sensor = esp_camera_sensor_get();
      sensor->set_hmirror(sensor, 1);
      sensor->set_vflip(sensor, 1);
      camera_in_use = true;
      return true;
    }
    if (err != 0x20004) {
      char buf[40];
      sprintf(buf, "Camera init failed with error %x", err);
      bot.sendMessage(CHAT_ID, buf);
    }

    // power off the camera and try again
    digitalWrite(PWDN_GPIO_NUM, HIGH);
    pinMode(PWDN_GPIO_NUM, OUTPUT);
    delay(3000);
  }
  // cannot init camera
  bot.sendMessage(CHAT_ID, "Camera init failed for 20 times");
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
      if (json.containsKey("heizung")) {
        command = HEIZUNG;
      }
      if (json.containsKey("image")) {
        if (init_camera()) {
          camera_fb_t* fb = esp_camera_fb_get();
          if (!fb) {
            Serial.println("Camera capture failed");
            delay(1000);
            ESP.restart();
          } else {
            if (fb->format == PIXFORMAT_JPEG) {
              webSocket.broadcastBIN(fb->buf, fb->len);
            } else if (fb->format == PIXFORMAT_GRAYSCALE) {
              int code_cnt = qr_identify(fb);
              for (int i = 0; i < code_cnt; i++) {
                uint16_t data[9];
                data[0] = 1;
                for (int j = 0; j < 4; j++) {
                  data[2 * j + 1] = qr_codes[i].corners[j].x;
                  data[2 * j + 2] = qr_codes[i].corners[j].y;
                }
                webSocket.broadcastBIN((uint8_t*)data, 18);
              }

              uint8_t head[5];
              head[0] = 0;
              head[1] = fb->width >> 8;
              head[2] = fb->width & 0xff;
              head[3] = fb->height >> 8;
              head[4] = fb->height & 0xff;
              webSocket.broadcastBIN(head, 5);
              webSocket.broadcastBIN(fb->buf, fb->len);
            }
            esp_camera_fb_return(fb);
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
      myObject["stack_free"] = (long)uxTaskGetStackHighWaterMark(NULL);
      // myObject["time"] = formattedTime;
      // myObject["b64"] = base64::encode((uint8_t*)data_buf, data_idx);
      // myObject["button_analog"] = analogRead(BUTTON_PIN);
      // myObject["button_digital"] = digitalRead(BUTTON_PIN);
      myObject["digital"] = data;
      // myObject["sample_rate"] = I2S_SAMPLE_RATE;
      myObject["wifi_dBm"] = WiFi.RSSI();
      myObject["SSID"] = WiFi.SSID();

      myObject["status"] = status;
      myObject["bootCount"] = bootCount;
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
      welcome += "/test : Show typing, then eply\n";
      welcome += "/nosleep : Prevent deep slep\n";
      welcome += "/allowsleep : Allow deep slep\n";
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

  secured_client.setCACert(
      TELEGRAM_CERTIFICATE_ROOT);  // Add root certificate for api.telegram.org

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
  server.on("/image", HTTP_GET, []() {
    bool send_error = true;
    sensor_t* sensor = esp_camera_sensor_get();
    sensor->set_pixformat(sensor, PIXFORMAT_JPEG);
    sensor->set_framesize(sensor, FRAMESIZE_UXGA);
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
    } else {
      if (fb->width > 100) {
        if (fb->format != PIXFORMAT_JPEG) {
          //          bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf,
          //          &_jpg_buf_len); fb = NULL; if(!jpeg_converted){
          //            Serial.println("JPEG compression failed");
          //          }
        } else {
          server.sendHeader("Connection", "close");
          server.send_P(200, "Content-Type: image/jpeg", (const char*)fb->buf,
                        fb->len);
          send_error = false;
        }
      }
      esp_camera_fb_return(fb);
    }
    if (send_error) {
      server.send(404);
      Serial.print("IMAGE ERROR: ");
      Serial.println(server.uri());
    }
  });
  server.on("/heizung", HTTP_GET, []() {
    command = HEIZUNG;
    server.sendHeader("Connection", "close");
    server.send_P(200, "text/html", (const char*)index_html_start);
  });
  server.on("/qr", HTTP_GET, []() {
    bool send_error = true;
#ifdef NEW
    app_qr_recognize();
    sensor_t* sensor = esp_camera_sensor_get();
    sensor->set_pixformat(sensor, PIXFORMAT_GRAYSCALE);
    sensor->set_framesize(sensor, FRAMESIZE_QVGA);
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
    } else {
      struct quirc* qr_recognizer = quirc_new();
      if (!qr_recognizer) {
        ESP_LOGE(TAG, "Can't create quirc object");

      } else {
        if (quirc_resize(qr_recognizer, fb->width, fb->height) < 0) {
          ESP_LOGE(
              TAG,
              "Resize the QR-code recognizer err (cannot allocate memory).");
        } else {
          int w, h;
          uint8_t* image = quirc_begin(qr_recognizer, &w, &h);
          if (image) {
            memcpy(image, fb->buf, w * h);
            quirc_end(qr_recognizer);

            // Return the number of QR-codes identified in the last processed
            // image.
            int id_count = quirc_count(qr_recognizer);

            server.sendHeader("Connection", "close");
            server.send(200, "text/plain",
                        id_count > 0 ? "QR-CODE" : "NO QR-CODE");
            send_error = false;
          } else {
            ESP_LOGE(TAG, "no image, dimension %d*%d != %d, %d*%d", w, h,
                     fb->len, fb->width, fb->height);
          }
        }
        ESP_LOGE(TAG, "Deconstruct QR-Code recognizer(quirc)");
        quirc_destroy(qr_recognizer);
      }
      esp_camera_fb_return(fb);
    }
#endif
    if (send_error) {
      server.send(404);
      Serial.print("IMAGE ERROR: ");
      Serial.println(server.uri());
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

  rc = xTaskCreatePinnedToCore(TaskWebSocket, "WebSocket", 8192, (void*)1, 1,
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
    case HEIZUNG: {
      // bot.sendMessage(CHAT_ID, WiFi.SSID() + String(": ") +
      // WiFi.localIP().toString());
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
        digitalWrite(flashPin, LOW);
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
      }
      command = DEEPSLEEP;
      break;
      case DEEPSLEEP:
        if (deepsleep) {
          // ensure LEDs and camera module off and pull up/downs set
          // appropriately
          digitalWrite(ledPin, HIGH);
          digitalWrite(flashPin, LOW);
          digitalWrite(PWDN_GPIO_NUM, HIGH);
          pinMode(ledPin, INPUT_PULLUP);
          pinMode(flashPin, INPUT_PULLDOWN);
          pinMode(PWDN_GPIO_NUM, INPUT_PULLUP);
          // wake up every four hours
          esp_sleep_enable_timer_wakeup(4LL * 3600LL * 1000000LL);
          esp_deep_sleep_start();
        }
        command = IDLE;
        break;
    }
  }
}
