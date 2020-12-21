#include "tpl_telegram.h"

#include <Arduino.h>
#include <UniversalTelegramBot.h>
#include <WebSockets.h>
#include <WebSocketsClient.h>
#include <WiFi.h>

#include "string.h"
#include "tpl_system.h"

using namespace std;

WiFiClientSecure secured_client;
static const char *BOTtoken = NULL;
static const char *chatId = NULL;

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

    if (text == "/nosleep") {
      tpl_config.allow_deepsleep = false;
      bot->sendMessage(chat_id, "Prevent sleep");
    }
    if (text == "/allowsleep") {
      tpl_config.allow_deepsleep = true;
      bot->sendMessage(chat_id, "Allow sleep");
    }

    if (text == "/start") {
      String welcome = "Welcome to Universal Arduino Telegram Bot library, " +
                       from_name + ".\n";
      welcome += "This is Chat Action Bot example.\n\n";
      welcome += "/test : Show typing, then eply\n";
      welcome += "/nosleep : Prevent deep slep\n";
      welcome += "/allowsleep : Allow deep slep\n";
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

      while (numNewMessages) {
        handleNewMessages(&bot, numNewMessages);
        numNewMessages = bot.getUpdates(bot.last_message_received + 1);
      }

      bot_lasttime = millis() + BOT_MTBS;
    }
    vTaskDelay(xDelay);
  }
}
//---------------------------------------------------
void tpl_telegram_setup(const char *bot_token, const char *chat_id) {
  // Add root certificate for api.telegram.org
  secured_client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
  BOTtoken = bot_token;
  chatId = chat_id;

  xTaskCreatePinnedToCore(TaskTelegramCore1, "Telegram", 6144, NULL, 1,
                          &tpl_tasks.task_telegram, CORE_1);
}
