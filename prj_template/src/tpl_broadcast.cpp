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
static uint8_t last_raw_packet[64];
static size_t last_raw_packet_size = 0;
static bool last_raw_valid = false;

#define RX_HISTORY_SIZE 16
struct RxEvent {
  unsigned long timestamp;
  int parse_size;
  size_t bytes_read;
  uint8_t first8[8];
  bool truncated;
  bool oversize;
  bool drained;
};
static RxEvent rx_history[RX_HISTORY_SIZE];
static uint8_t rx_history_head = 0;
static uint8_t rx_history_count = 0;
static uint32_t total_truncated = 0;
static uint32_t total_oversize = 0;

static void rx_history_add(int parse_size, size_t bytes_read, uint8_t* data,
                           bool truncated, bool oversize, bool drained) {
  RxEvent& e = rx_history[rx_history_head];
  e.timestamp = millis();
  e.parse_size = parse_size;
  e.bytes_read = bytes_read;
  e.truncated = truncated;
  e.oversize = oversize;
  e.drained = drained;
  size_t copy_n = bytes_read < 8 ? bytes_read : 8;
  memcpy(e.first8, data, copy_n);
  if (copy_n < 8) memset(e.first8 + copy_n, 0, 8 - copy_n);
  rx_history_head = (rx_history_head + 1) % RX_HISTORY_SIZE;
  if (rx_history_count < RX_HISTORY_SIZE) rx_history_count++;
}

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
  last_raw_valid = true;
}

int tpl_broadcast_get_reinit_count() { return reinit_count; }
uint32_t tpl_broadcast_get_total_parsed() { return total_parsed; }
uint32_t tpl_broadcast_get_total_drained() { return total_drained; }
uint32_t tpl_broadcast_get_total_truncated() { return total_truncated; }
uint32_t tpl_broadcast_get_total_oversize() { return total_oversize; }

size_t tpl_broadcast_get_rx_history(void* dst, size_t dst_size, uint8_t* out_count) {
  *out_count = rx_history_count;
  if (rx_history_count == 0) return 0;
  uint8_t start = (rx_history_head + RX_HISTORY_SIZE - rx_history_count) % RX_HISTORY_SIZE;
  uint8_t* out = (uint8_t*)dst;
  size_t written = 0;
  for (uint8_t i = 0; i < rx_history_count; i++) {
    size_t entry_sz = sizeof(RxEvent);
    if (written + entry_sz > dst_size) break;
    memcpy(out + written, &rx_history[(start + i) % RX_HISTORY_SIZE], entry_sz);
    written += entry_sz;
  }
  return written;
}

size_t tpl_broadcast_get_last_raw(uint8_t* dst, size_t dst_size, bool* was_valid) {
  size_t sz = last_raw_packet_size < dst_size ? last_raw_packet_size : dst_size;
  memcpy(dst, last_raw_packet, sz);
  if (was_valid) *was_valid = last_raw_valid;
  return last_raw_packet_size;
}

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

  for (;;) {
    int packetSize = udp.parsePacket();
    if (packetSize <= 0) break;

    total_parsed++;

    if (packetSize > (int)buffer_size) {
      total_oversize++;
      rx_history_add(packetSize, 0, (uint8_t*)buffer, false, true, false);
      udp.flush();
      continue;
    }

    size_t bytes_read = udp.read((uint8_t*)buffer, packetSize);
    udp.flush();

    if (bytes_read > 0) {
      bool truncated = (bytes_read != (size_t)packetSize);
      if (truncated) total_truncated++;
      if (bytes_read <= sizeof(last_raw_packet)) {
        memcpy(last_raw_packet, buffer, bytes_read);
        last_raw_packet_size = bytes_read;
      }
      last_raw_valid = false;
      if (received_size != NULL) {
        *received_size = bytes_read;
      }
      bool was_drained = got_packet;
      if (was_drained) total_drained++;
      rx_history_add(packetSize, bytes_read, (uint8_t*)buffer, truncated, false, was_drained);
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
