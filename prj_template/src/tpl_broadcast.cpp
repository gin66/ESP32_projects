#include "tpl_broadcast.h"

#include "tpl_command.h"
#include "tpl_system.h"
#ifdef IS_ESP32CAM
#include "tpl_esp_camera.h"
#endif

WiFiUDP udp;
static const char *broadcast = NULL;
static uint16_t port;

void tpl_broadcast_setup(uint16_t udpPort, const char *broadcastAddress) {
  broadcast = broadcastAddress;
  port = udpPort;
  udp.begin(port);
}
void tpl_broadcast(uint8_t *packet, uint8_t length) {
  if (broadcast != NULL) {
    udp.beginPacket(broadcast, port);
    udp.write(packet, length);
    udp.endPacket();
  }
}
