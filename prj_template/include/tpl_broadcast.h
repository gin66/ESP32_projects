#include <WiFi.h>
#include <WiFiUdp.h>
// broadcast
struct stromzaehler_packet_s {
   uint8_t tm_sec;
   uint8_t tm_min;
   uint8_t tm_hour;
   uint8_t tm_wday;
   float consumption_Wh;
   float production_Wh;
   float current_W;
};

void tpl_broadcast_setup(uint16_t udpPort = 56374, const char *broadcastAddress = "192.168.1.255");
void tpl_broadcast(uint8_t *packet, uint8_t length);
