#ifdef BOTtoken
#include "tpl_telegram.h"

#include <Arduino.h>
#include <UniversalTelegramBot.h>
#include <WebSockets.h>
#include <WebSocketsClient.h>
#include <WiFi.h>

#include "string.h"
#include "tpl_system.h"
#include "tpl_command.h"
#ifdef IS_ESP32CAM
#include "tpl_esp_camera.h"
#endif

WiFiClientSecure secured_client;
static const char *BOTtoken = NULL;
static const char *chatId = NULL;

static camera_fb_t* photo_fb = NULL;
static uint32_t dataBytesSent;
static bool isMoreDataAvailable() {
	Serial.println("isMoreDataAvailable");
  if (photo_fb) {
	  if (dataBytesSent >= photo_fb->len) {
          esp_camera_fb_return(photo_fb);
          photo_fb = NULL;
	Serial.println("Done");
		  return false;
	  }
	  else {
		  return true;
	  }
  } else {
    return false;
  }
}
static byte* getNextBuffer() {
	Serial.println("getNextBuffer");
  if (photo_fb) {
    byte* buf = &photo_fb->buf[dataBytesSent];
    dataBytesSent += 1024;
    return buf;
  } else {
    return nullptr;
  }
}
static int getNextBufferLen() {
	Serial.println("getNextBufferLen");
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

  UniversalTelegramBot bot(BOTtoken, secured_client);

  for (;;) {
    if (millis() >= bot_lasttime) {
      int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

      if (numNewMessages) {
        handleNewMessages(&bot, numNewMessages);
      }
      bot_lasttime = millis() + BOT_MTBS;
    }
#ifdef IS_ESP32CAM
	if (tpl_config.bot_send_jpg_image) {
		tpl_config.bot_send_jpg_image = false;
        // digitalWrite(flashPin, HIGH);
        // for (uint8_t i = 0; i < 10; i++) {
          // let the camera adjust
        //   photo_fb = esp_camera_fb_get();
        //   esp_camera_fb_return(photo_fb);
        //   photo_fb = NULL;
        // }
		if (photo_fb == NULL) {
        photo_fb = esp_camera_fb_get();
        //digitalWrite(flashPin, LOW);
        if (!photo_fb) {
          bot.sendMessage(chatId, "Camera capture failed");
        } else {
          dataBytesSent = 0;
          bot.sendPhotoByBinary(chatId, "image/jpeg", photo_fb->len, isMoreDataAvailable, nullptr, getNextBuffer, getNextBufferLen);
        }
		}
	}
#endif
    vTaskDelay(xDelay);
  }
}
//---------------------------------------------------
void tpl_telegram_setup(const char *bot_token, const char *chat_id) {
  // Add root certificate for api.telegram.org
  secured_client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
  BOTtoken = bot_token;
  chatId = chat_id;

  xTaskCreatePinnedToCore(TaskTelegramCore1, "Telegram", 6192, NULL, 1,
                          &tpl_tasks.task_telegram, CORE_1);
}
#endif
