#ifdef BOTtoken
#include "tpl_telegram.h"

#include <Arduino.h>
#include <UniversalTelegramBot.h>
#include <WebSockets.h>
#include <WebSocketsClient.h>
#include <WiFi.h>

#include "string.h"
#include "tpl_command.h"
#include "tpl_system.h"
#ifdef IS_ESP32CAM
#include "tpl_esp_camera.h"
#endif

static const char *chatId = NULL;

static uint8_t *jpeg_to_send = NULL;
static size_t jpeg_len = 0;
static size_t dataBytesSent;
static bool isMoreDataAvailable() {
  if (jpeg_to_send) {
    if (dataBytesSent >= jpeg_len) {
      return false;
    } else {
      return true;
    }
  } else {
    return false;
  }
}
#define CHUNKSIZE 256
static byte *getNextBuffer() {
  if (jpeg_to_send) {
    byte *buf = &jpeg_to_send[dataBytesSent];
    // dataBytesSent += CHUNKSIZE;
    return buf;
  } else {
    return nullptr;
  }
}
static int getNextBufferLen() {
  if (jpeg_to_send) {
    uint32_t rem = jpeg_len - dataBytesSent;
    dataBytesSent += CHUNKSIZE;  // getNextBuffer is called first !!!!
    if (rem > CHUNKSIZE) {
      return CHUNKSIZE;
    } else {
      return rem;
    }
  } else {
    return 0;
  }
}

void handleNewMessages(UniversalTelegramBot *bot, int numNewMessages) {
  Serial.println("handleNewMessages");
  Serial.println(String(numNewMessages));

  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = bot->messages[i].chat_id;
    String text = bot->messages[i].text;

    String from_name = bot->messages[i].from_name;
    if (from_name == "") from_name = "Guest";

    if (text == "/test") {
      bot->sendChatAction(chat_id, "typing");
      delay(4000);
      bot->sendMessage(chat_id, "Did you see the action message?");
    }
    if (text == "/deepsleep") {
      tpl_config.allow_deepsleep = true;
      tpl_command = CmdDeepSleep;
      bot->sendMessage(chat_id, "Go to sleep");
    }
    if (text == "/nosleep") {
      tpl_config.allow_deepsleep = false;
      bot->sendMessage(chat_id, "Prevent sleep");
    }
    if (text == "/allowsleep") {
      tpl_config.allow_deepsleep = true;
      bot->sendMessage(chat_id, "Allow sleep");
    }
#ifdef IS_ESP32CAM
    if (text == "/flash") {
      tpl_command = CmdFlash;
      bot->sendMessage(chat_id, "Toggle flash");
    }
    if (text == "/image") {
      tpl_command = CmdSendJpg2Bot;
      bot->sendMessage(chat_id, "Take picture");
    }
#endif

    if (text == "/start") {
      String welcome = "Welcome to Universal Arduino Telegram Bot library, " +
                       from_name + ".\n";
      welcome += "/test : Show typing, then reply\n";
      welcome += "/deepsleep : Go to deep sleep\n";
      welcome += "/nosleep : Prevent deep sleep\n";
      welcome += "/allowsleep : Allow deep sleep\n";
#ifdef IS_ESP32CAM
      welcome += "/image : Send image\n";
#endif
      bot->sendMessage(chat_id, welcome);
    }
  }
}

void TaskTelegramCore1(void *pvParameters) {
  const unsigned long BOT_MTBS = 1000;  // mean time between scan messages
  unsigned long bot_lasttime = 0;  // last time messages' scan has been done
  const TickType_t xDelay = 10 / portTICK_PERIOD_MS;

  WiFiClientSecure secured_client;
  // Add root certificate for api.telegram.org
  secured_client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
  UniversalTelegramBot bot(tpl_config.receive_bot_token, secured_client);

  for (;;) {
    if (millis() >= bot_lasttime) {
      bot.updateToken(tpl_config.receive_bot_token);
      tpl_config.bot_communication_ongoing = true;
      int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
      tpl_config.bot_communication_ongoing = false;

      if (numNewMessages) {
        handleNewMessages(&bot, numNewMessages);
      }
      bot_lasttime = millis() + BOT_MTBS;
    }
#ifdef IS_ESP32CAM
    if (tpl_config.bot_send_jpg_image) {
      bot.updateToken(tpl_config.send_bot_token);
      jpeg_to_send = tpl_config.curr_jpg;
      jpeg_len = tpl_config.curr_jpg_len;
      camera_fb_t *fb = NULL;
      if (jpeg_to_send == NULL) {
        // take picture if needed
        Serial.println("take picture");
        uint32_t settle_till = millis() + 1000;
        while ((int32_t)(settle_till - millis()) > 0) {
          // let the camera adjust
          fb = esp_camera_fb_get();
          if (fb) {
            esp_camera_fb_return(fb);
            fb = NULL;
          }
          vTaskDelay(xDelay);
        }
        fb = esp_camera_fb_get();
        if (fb) {
          jpeg_to_send = fb->buf;
          jpeg_len = fb->len;
        }
      }
      if (jpeg_to_send != NULL) {
        Serial.print("send image in bytes=");
        Serial.println(jpeg_len);
        tpl_config.bot_communication_ongoing = true;
        bot.sendMessage(chatId, String("Send image: ") + jpeg_len +
                                    String(" from ") + String(HOSTNAME) +
                                    String(" BootCnt=") + tpl_config.bootCount);
        tpl_config.bot_communication_ongoing = false;
        for (uint8_t retry = 0; retry < 5; retry++) {
          dataBytesSent = 0;
          WATCH(3001)
          Serial.println("before sendPhotoByBinary");
          // can hang here:
          // [E][ssl_client.cpp:36] _handle_error(): [send_ssl_data():300]:
          // (-26752) SSL - Connection requires a write call Afterwards
          // wifi_stop() yields [E][WiFiClient.cpp:395] write(): fail on fd 60,
          // errno: 5, "I/O error"
          //
          // After entering deepsleep, there is no wakeup
          tpl_config.bot_communication_ongoing = true;
          String res = bot.sendPhotoByBinary(chatId, "image/jpeg", jpeg_len,
                                             isMoreDataAvailable, nullptr,
                                             getNextBuffer, getNextBufferLen);
          tpl_config.bot_communication_ongoing = false;
          WATCH(3002)
          if (res.length() > 0) {
            break;
          }
		  Serial.println("after sendPhotoByBinary..retry");
        }
        Serial.println("after sendPhotoByBinary");
      } else {
        bot.sendMessage(chatId, "Camera capture failed");
      }
      if (fb) {
        esp_camera_fb_return(fb);
      }
      WATCH(3009)
      tpl_config.bot_send_jpg_image = false;
    }
#endif
    if (tpl_config.bot_send_message) {
      bot.updateToken(tpl_config.send_bot_token);
      if (tpl_config.bot_message) {
        Serial.print("Send message: ");
        Serial.println(tpl_config.bot_message);
        tpl_config.bot_communication_ongoing = true;
        bot.sendMessage(chatId, tpl_config.bot_message);
        tpl_config.bot_communication_ongoing = false;
      }
      tpl_config.bot_message = NULL;
      tpl_config.bot_send_message = false;
    }
    vTaskDelay(xDelay);
  }
}
//---------------------------------------------------
void tpl_telegram_setup(const char *chat_id) {
  chatId = chat_id;

  xTaskCreatePinnedToCore(TaskTelegramCore1, "Telegram", 6072, NULL, 1,
                          &tpl_tasks.task_telegram, CORE_1);
}
#endif
