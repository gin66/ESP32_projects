#include <WebSockets.h>
#include <WebSocketsClient.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>  // already included in UniversalTelegramBot.h

extern WebSocketsServer webSocket;

void tpl_websocket_setup(void (*func)(DynamicJsonDocument *json));
