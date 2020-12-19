#include <ArduinoOTA.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <ESPmDNS.h>

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
  bool need_connect = true;
  if (last_connected_network != -1) {
    if (verbose) {
      Serial.print("Try last connected network: ");
      Serial.println(nets[last_connected_network].ssid);
    }
    WiFi.begin(nets[last_connected_network].ssid,
               nets[last_connected_network].passwd);
    if (WiFi.waitForConnectResult() == WL_CONNECTED) {
      need_connect = false;
      if (verbose) {
        Serial.println("");
        Serial.println("WiFi connected");
        Serial.println("IP address: ");
        Serial.println(WiFi.localIP());
      }
    } else {
      WiFi.disconnect(/*wifioff=*/true);
    }
  }
  if (need_connect) {
    if (verbose) {
      Serial.println("Connecting Wifi...");
    }
    connect();
  }

  if (MDNS.begin(HOSTNAME)) {
    Serial.println("MDNS responder started");
	MDNS.addService("http", "tcp", 80);
	MDNS.addService("ws", "tcp", 81);
  }

  // Port defaults to 3232
  // ArduinoOTA.setPort(3232);

  // Hostname defaults to esp3232-[MAC]
  ArduinoOTA.setHostname(HOSTNAME);

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  if (verbose) {
    ArduinoOTA
        .onStart([]() {
          String type;
          if (ArduinoOTA.getCommand() == U_FLASH)
            type = "sketch";
          else  // U_SPIFFS
            type = "filesystem";

          // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS
          // using SPIFFS.end()
          Serial.println("Start updating " + type);
        })
        .onEnd([]() { Serial.println("\nEnd"); })
        .onProgress([](unsigned int progress, unsigned int total) {
          Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
        })
        .onError([](ota_error_t error) {
          Serial.printf("Error[%u]: ", error);
          if (error == OTA_AUTH_ERROR)
            Serial.println("Auth Failed");
          else if (error == OTA_BEGIN_ERROR)
            Serial.println("Begin Failed");
          else if (error == OTA_CONNECT_ERROR)
            Serial.println("Connect Failed");
          else if (error == OTA_RECEIVE_ERROR)
            Serial.println("Receive Failed");
          else if (error == OTA_END_ERROR)
            Serial.println("End Failed");
        });
  }

  ArduinoOTA.begin();
}
void my_wifi_loop(bool verbose) {
  if (WiFi.status() != WL_CONNECTED) {
    connect();
  }
  ArduinoOTA.handle();
}
