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
#include <esp_wifi.h>
#include <mem.h>

#include "../../private_bot.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp32-hal-psram.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "fb_gfx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "img_converters.h"
#include "template.h"

#define DEBUG_ESP

#ifdef DEBUG_ESP
#define DBG(x) Serial.println(x)
#else
#define DBG(...)
#endif

#define ledPin ((gpio_num_t)33)
#define flashPin ((gpio_num_t)4)

using namespace std;

volatile bool status = false;
volatile bool deepsleep = true;

RTC_DATA_ATTR uint16_t bootCount = 0;

WebServer server(80);
extern const uint8_t index_html_start[] asm("_binary_src_index_html_start");
extern const uint8_t server_index_html_start[] asm(
    "_binary_src_serverindex_html_start");

WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOTtoken, secured_client);

enum Command {
  IDLE,
  DEEPSLEEP,
} command = IDLE;

WebSocketsServer webSocket = WebSocketsServer(81);

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
      if (json.containsKey("image")) {
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
  server.on("/deepsleep", HTTP_GET, []() {
    command = DEEPSLEEP;
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
  server.begin();

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
    case DEEPSLEEP:
      if (deepsleep) {
        // ensure LEDs and camera module off and pull up/downs set
        // appropriately
        digitalWrite(ledPin, HIGH);
        digitalWrite(flashPin, LOW);
        pinMode(ledPin, INPUT_PULLUP);
        pinMode(flashPin, INPUT_PULLDOWN);

#ifdef OLD
        rtc_gpio_isolate(ledPin);
        rtc_gpio_isolate(flashPin);
        rtc_gpio_isolate((gpio_num_t)2);
        rtc_gpio_isolate((gpio_num_t)12);
        rtc_gpio_isolate((gpio_num_t)13);
        rtc_gpio_isolate((gpio_num_t)14);
        rtc_gpio_isolate((gpio_num_t)15);
        rtc_gpio_isolate((gpio_num_t)26);
        rtc_gpio_isolate((gpio_num_t)27);
        rtc_gpio_isolate((gpio_num_t)0);
#endif
        pinMode((gpio_num_t)2, INPUT_PULLUP);
        pinMode((gpio_num_t)12, INPUT_PULLUP);
        pinMode((gpio_num_t)13, INPUT_PULLUP);
        pinMode((gpio_num_t)14, INPUT_PULLUP);
        pinMode((gpio_num_t)15, INPUT_PULLUP);
        pinMode((gpio_num_t)26, INPUT_PULLUP);
        pinMode((gpio_num_t)27, INPUT_PULLUP);
        pinMode((gpio_num_t)0, INPUT_PULLUP);

        tpl_camera_off();

        gpio_deep_sleep_hold_en();
        esp_wifi_stop();

        // wake up every four hours
        esp_sleep_enable_timer_wakeup(4LL * 3600LL * 1000000LL);
        esp_deep_sleep_start();
      }
      command = IDLE;
      break;
  }
}