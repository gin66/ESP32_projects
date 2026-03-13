#include "tpl_broadcast.h"

#include <string.h>

#include "tpl_command.h"
#include "tpl_system.h"
#include "tpl_wifi.h"

WiFiUDP udp;
static const char* broadcast = NULL;
static uint16_t port;

static void tpl_broadcast_reinit(void);

void tpl_broadcast_setup(uint16_t udpPort, const char* broadcastAddress) {
  broadcast = broadcastAddress;
  port = udpPort;
  udp.begin(port);
  tpl_wifi_register_reconnect(tpl_broadcast_reinit);
}
void tpl_broadcast(uint8_t* packet, uint8_t length) {
  if (broadcast != NULL) {
    udp.beginPacket(broadcast, port);
    udp.write(packet, length);
    udp.endPacket();
  }
}

bool tpl_broadcast_receive(void* buffer, size_t buffer_size,
                           size_t* received_size) {
  int packetSize = udp.parsePacket();
  if (packetSize > 0 && packetSize <= buffer_size) {
    size_t bytes_read = udp.read((uint8_t*)buffer, packetSize);

    if (received_size != NULL) {
      *received_size = bytes_read;
    }

    return bytes_read > 0;
  }
  return false;
}

void tpl_broadcast_reinit(void) {
  if (port > 0) {
    Serial.printf("Broadcast: reinitializing UDP on port %d\n", port);
    udp.stop();
    udp.begin(port);
  }
}
