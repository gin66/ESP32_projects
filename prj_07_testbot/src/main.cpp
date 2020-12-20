#include "string.h"
//#include "soc/soc.h"
//#include "soc/rtc_cntl_reg.h"

#include <Arduino.h>
#include <ArduinoOTA.h>
//#include <ArduinoJson.h>  // already included in UniversalTelegramBot.h
#include <ESP32Ping.h>
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

#define BOTtoken Heizung_BOTtoken

WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOTtoken, secured_client);

void TaskCommandCore1(void* pvParameters);

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

#define WS_BUFLEN 4096
char ws_buffer[WS_BUFLEN];
void TaskWebSocketCore0(void* pvParameters) {
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
      /* size_t bx = */ serializeJson(myObject, &ws_buffer, WS_BUFLEN);
      String as_json = String(ws_buffer);
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

  tpl_wifi_setup(true);

  // Add root certificate for api.telegram.org
  secured_client.setCACert(TELEGRAM_CERTIFICATE_ROOT);

  tpl_webserver_setup();

  BaseType_t rc;

#define CORE0 0
#define CORE1 1
  rc = xTaskCreatePinnedToCore(TaskWebSocketCore0, "WebSocket", 4096, NULL, 1,
                               &tpl_tasks.task_web_socket, CORE0);
  if (rc != pdPASS) {
    Serial.print("cannot start websocket task=");
    Serial.println(rc);
  }

  rc = xTaskCreatePinnedToCore(TaskCommandCore1, "Command", 2048, NULL, 1,
                               &tpl_tasks.task_command, CORE1);
  if (rc != pdPASS) {
    Serial.print("cannot start websocket task=");
    Serial.println(rc);
  }

  startNetWatchDog();

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
  Serial.print("Stackfree: loop=");
  Serial.print(uxTaskGetStackHighWaterMark(NULL));
  Serial.print(" net_watchdog=");
  Serial.print(uxTaskGetStackHighWaterMark(tpl_tasks.task_net_watchdog));
  Serial.print(" websocket=");
  Serial.print(uxTaskGetStackHighWaterMark(tpl_tasks.task_web_socket));
  Serial.print(" command=");
  Serial.print(uxTaskGetStackHighWaterMark(tpl_tasks.task_command));
  Serial.print(" wifi=");
  Serial.print(uxTaskGetStackHighWaterMark(tpl_tasks.task_wifi_manager));
  Serial.print(" http=");
  Serial.println(uxTaskGetStackHighWaterMark(tpl_tasks.task_webserver));

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
  const TickType_t xDelay = 10 / portTICK_PERIOD_MS;
  vTaskDelay(xDelay);
}

void TaskCommandCore1(void* pvParameters) {
  const TickType_t xDelay = 10 / portTICK_PERIOD_MS;

  for (;;) {
    switch (command) {
      case IDLE:
        break;
      case DEEPSLEEP:
        if (deepsleep) {
          esp_wifi_stop();

          // wake up every four hours
          esp_sleep_enable_timer_wakeup(4LL * 3600LL * 1000000LL);
          esp_deep_sleep_start();
        }
        command = IDLE;
        break;
    }
    vTaskDelay(xDelay);
  }
}
