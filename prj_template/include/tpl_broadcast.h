#include <WiFi.h>
#include <WiFiUdp.h>
#include <stdint.h>
#include <stddef.h>

// broadcast
#define STROMZAEHLER_PORT 56374
#define MAX_PACKET_SIZE 256

struct stromzaehler_packet_s {
   uint8_t tm_sec;
   uint8_t tm_min;
   uint8_t tm_hour;
   uint8_t tm_wday;
   float consumption_Wh;
   float production_Wh;
   float current_W;
};

void tpl_broadcast_setup(uint16_t udpPort = STROMZAEHLER_PORT, const char *broadcastAddress = "192.168.1.255");
void tpl_broadcast(uint8_t *packet, uint8_t length);
bool tpl_broadcast_receive(void *buffer, size_t buffer_size, size_t *received_size = NULL);
void tpl_broadcast_set_receive_callback(void (*callback)(void *data, size_t size));
