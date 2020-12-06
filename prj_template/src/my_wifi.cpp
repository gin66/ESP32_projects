#include <WiFi.h>
#include <WiFiMulti.h>

#include "wifi_secrets.h"

RTC_DATA_ATTR int last_connected_network = -1;
bool automode = false;

WiFiMulti wifiMulti;

static void connect() {
  if (!automode) {
    automode = true;
    const struct net_s *net = &nets[0];
    while (net->ssid) {
      wifiMulti.addAP(net->ssid, net->passwd);
      net++;
    }
  }
  if (wifiMulti.run() == WL_CONNECTED) {
    const struct net_s *net = &nets[0];
    String ssid = WiFi.SSID();
    for (int i = 0; net->ssid; i++, net++) {
      if (ssid.equals(net->ssid)) {
        last_connected_network = i;
        break;
      }
    }
  }
}

void my_wifi_setup(bool verbose) {
  if (last_connected_network != -1) {
    if (true) {
      Serial.print("Try last connected network: ");
      Serial.println(nets[last_connected_network].ssid);
    }
    WiFi.begin(nets[last_connected_network].ssid,
               nets[last_connected_network].passwd);
    if (WiFi.waitForConnectResult() == WL_CONNECTED) {
      if (true) {
        Serial.println("");
        Serial.println("WiFi connected");
        Serial.println("IP address: ");
        Serial.println(WiFi.localIP());
      }
      return;
    }
    WiFi.disconnect(/*wifioff=*/true);
  }

  if (verbose) {
    Serial.println("Connecting Wifi...");
  }
  connect();
}
void my_wifi_loop(bool verbose) {
  if (WiFi.status() != WL_CONNECTED) {
    connect();
  }
}
