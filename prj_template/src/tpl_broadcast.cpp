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
static unsigned long last_valid_ms = 0;
static int reinit_count = 0;
static int consecutive_failed_reinits = 0;
static bool last_packet_valid = false;
static uint32_t total_parsed = 0;
static uint32_t total_drained = 0;
#ifndef MAX_SIMPLE_REINITS
#define MAX_SIMPLE_REINITS 3
#endif

static void tpl_broadcast_reinit(unsigned long current_ms);

void tpl_broadcast_setup(uint16_t udpPort, const char* broadcastAddress) {
  broadcast = broadcastAddress;
  port = udpPort;
  last_valid_ms = millis();
  last_receive_ms = millis();
  udp.begin(port);
  tpl_wifi_register_reconnect(tpl_broadcast_reinit);
}

void tpl_broadcast_force_reinit() {
  reinit_count++;
  Serial.printf("Broadcast: force reinit #%d (full WiFi reconnect)\n", reinit_count);
  udp.stop();
  WiFi.disconnect(false, true);
  delay(100);
  WiFi.begin();
  delay(500);
  udp.begin(port);
  last_reinit_ms = millis();
}

void tpl_broadcast_report_valid() {
  last_packet_valid = true;
}

int tpl_broadcast_get_reinit_count() { return reinit_count; }
uint32_t tpl_broadcast_get_total_parsed() { return total_parsed; }
uint32_t tpl_broadcast_get_total_drained() { return total_drained; }
uint16_t tpl_broadcast_get_port() { return port; }
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

    total_parsed++;

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
      if (got_packet) total_drained++;
      got_packet = true;
      last_receive_ms = now;
    }
  }

  if (got_packet) {
    return true;
  }

  if (last_packet_valid) {
    consecutive_failed_reinits = 0;
    last_valid_ms = now;
    last_packet_valid = false;
  }

  // Self-healing: reinit UDP socket if no valid packet received for 10s
  // Protects against silent UDP socket death on ESP32 (lwIP issue)
  if ((now - last_reinit_ms > 10000) &&
      (last_valid_ms == 0 || now - last_valid_ms > 10000)) {
    if (consecutive_failed_reinits >= MAX_SIMPLE_REINITS) {
      Serial.printf(
          "Broadcast: %d simple reinits failed, escalating to full WiFi "
          "reconnect\n",
          consecutive_failed_reinits);
      tpl_broadcast_force_reinit();
      consecutive_failed_reinits = 0;
    } else {
      tpl_broadcast_reinit(now);
      consecutive_failed_reinits++;
    }
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
