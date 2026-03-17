#include "tpl_broadcast.h"

#include <string.h>

#include "tpl_command.h"
#include "tpl_system.h"
#include "tpl_wifi.h"

WiFiUDP udp;
static const char* broadcast = NULL;
static uint16_t port;
static unsigned long last_receive_ms = 0;
static unsigned long last_reinit_ms = 0;

static void tpl_broadcast_reinit(unsigned long current_ms);

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
  unsigned long now = millis();
  bool got_packet = false;

  // Drain all pending UDP packets, keeping only the latest valid one.
  // This prevents queue buildup when the main loop is slower than the
  // sender rate (e.g. due to LVGL rendering blocking the loop).
  for (;;) {
    int packetSize = udp.parsePacket();
    if (packetSize <= 0) break;

    if (packetSize > buffer_size) {
      udp.flush();
      continue;
    }

    size_t bytes_read = udp.read((uint8_t*)buffer, packetSize);
    udp.flush();

    if (bytes_read > 0) {
      if (received_size != NULL) {
        *received_size = bytes_read;
      }
      got_packet = true;
      last_receive_ms = now;
    }
  }

  if (got_packet) return true;

  // Self-healing: reinit UDP socket if no packet received for 60s
  // Protects against silent UDP socket death on ESP32 (lwIP issue)
  if ((now - last_reinit_ms > 60000) && (now - last_receive_ms > 60000)) {
    tpl_broadcast_reinit(now);
  }

  return false;
}

void tpl_broadcast_reinit(unsigned long current_ms) {
  if (port > 0) {
    Serial.printf("Broadcast: reinitializing UDP on port %d\n", port);
    udp.stop();
    udp.begin(port);
    last_reinit_ms = current_ms;
  }
}
