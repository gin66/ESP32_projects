#include <WiFi.h>
#include <WiFiUdp.h>

void tpl_broadcast_setup(uint16_t udpPort = 56374, const char *broadcastAddress = "192.168.1.255");
void tpl_broadcast(uint8_t *packet, uint8_t length);
