#include <ArduinoJson.h>  // already included in UniversalTelegramBot.h
#include <WebSockets.h>
#include <WebSocketsClient.h>
#include <WebSocketsServer.h>

extern WebSocketsServer webSocket;

void tpl_websocket_setup(void (*func)(DynamicJsonDocument *json));
