#include "tpl_wifi.h"

#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <inttypes.h>
#include <WiFiMulti.h>
#include <esp_task_wdt.h>

#include "tpl_system.h"
#include "wifi_secrets.h"

#ifndef MAX_WIFI_RECONNECT_CALLBACKS
#define MAX_WIFI_RECONNECT_CALLBACKS 8
#endif
#ifndef RSSI_RECONNECT_THRESHOLD
#define RSSI_RECONNECT_THRESHOLD -75
#endif
#ifndef RSSI_RECONNECT_SUSTAINED_SEC
#define RSSI_RECONNECT_SUSTAINED_SEC 120
#endif

RTC_DATA_ATTR int last_connected_network = -1;
bool automode = false;

WiFiMulti wifiMulti;

static tpl_wifi_reconnect_callback_t
    wifi_reconnect_callbacks[MAX_WIFI_RECONNECT_CALLBACKS];
static int wifi_reconnect_callback_count = 0;
static bool callback_fired = false;

void tpl_wifi_register_reconnect(tpl_wifi_reconnect_callback_t callback) {
  if (wifi_reconnect_callback_count < MAX_WIFI_RECONNECT_CALLBACKS) {
    wifi_reconnect_callbacks[wifi_reconnect_callback_count++] = callback;
  }
}

static void connect() {
  if (!automode) {
    automode = true;
    const struct net_s* net = &nets[0];
    while (net->ssid) {
      wifiMulti.addAP(net->ssid, net->passwd);
      net++;
    }
  }
  if (wifiMulti.run() == WL_CONNECTED) {
    const struct net_s* net = &nets[0];
    String ssid = WiFi.SSID();
    for (int i = 0; net->ssid; i++, net++) {
      if (ssid.equals(net->ssid)) {
        last_connected_network = i;
        break;
      }
    }
  }
}

static int rssi_roam_reconnect_count = 0;

void TaskWifiManager(void* pvParameters) {
  const TickType_t xDelay = 100 / portTICK_PERIOD_MS;
  uint32_t last_ok_ms = millis();
  uint32_t weak_rssi_since_ms = 0;

  for (;;) {
    esp_task_wdt_reset();

    if (!tpl_config.wifi_manager_shutdown_request) {
      wl_status_t status = WiFi.status();
      IPAddress ip = WiFi.localIP();
      bool has_valid_ip =
          (ip[0] != 0 || ip[1] != 0 || ip[2] != 0 || ip[3] != 0);
      bool connected = (status == WL_CONNECTED) && has_valid_ip;

      if (connected) {
        last_ok_ms = millis();

        int rssi = WiFi.RSSI();
        if (rssi < RSSI_RECONNECT_THRESHOLD) {
          if (weak_rssi_since_ms == 0) weak_rssi_since_ms = millis();
          if ((millis() - weak_rssi_since_ms) / 1000 >= RSSI_RECONNECT_SUSTAINED_SEC) {
            uint8_t* bssid = WiFi.BSSID();
            Serial.printf("WiFi: weak signal %d dBm for %" PRIu32 "s (BSSID %02X:%02X:%02X:%02X:%02X:%02X ch%d), reconnecting (#%d)\n",
                          rssi, (millis() - weak_rssi_since_ms) / 1000,
                          bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
                          WiFi.channel(), rssi_roam_reconnect_count + 1);
            WiFi.disconnect(true);
            vTaskDelay(100 / portTICK_PERIOD_MS);
            connect();
            rssi_roam_reconnect_count++;
            weak_rssi_since_ms = 0;
          }
        } else {
          weak_rssi_since_ms = 0;
        }

        if (tpl_config.wifi_recovery_tier != 0) {
          Serial.printf("WiFi: recovered, tier=%d->0\n",
                        tpl_config.wifi_recovery_tier);
          tpl_config.wifi_recovery_tier = 0;
          callback_fired = false;
        }
        if (!callback_fired) {
          callback_fired = true;
          for (int i = 0; i < wifi_reconnect_callback_count; i++) {
            if (wifi_reconnect_callbacks[i]) {
              wifi_reconnect_callbacks[i](last_ok_ms);
            }
          }
        }
        ArduinoOTA.handle();
      } else {
        weak_rssi_since_ms = 0;
        uint32_t secs_disconnected = (millis() - last_ok_ms) / 1000;

        if (!has_valid_ip && status == WL_CONNECTED) {
          Serial.println("WiFi: IP is 0.0.0.0, force disconnect");
          WiFi.disconnect(true);
          vTaskDelay(100 / portTICK_PERIOD_MS);
        }

        if (secs_disconnected > 120 && tpl_config.wifi_recovery_tier < 2) {
          Serial.printf("WiFi: Tier 2 - stack reset (disconnected %" PRIu32 "s)\n",
                        secs_disconnected);
          WATCH(21);
          tpl_config.wifi_recovery_tier = 2;
          WiFi.mode(WIFI_OFF);
          vTaskDelay(1000 / portTICK_PERIOD_MS);
          WiFi.mode(WIFI_STA);
          connect();
        } else if (secs_disconnected > 30 &&
                   tpl_config.wifi_recovery_tier < 1) {
          Serial.printf("WiFi: Tier 1 - reconnect (disconnected %" PRIu32 "s)\n",
                        secs_disconnected);
          WATCH(11);
          tpl_config.wifi_recovery_tier = 1;
          WiFi.reconnect();
        } else if (tpl_config.wifi_recovery_tier == 0) {
          Serial.println("WiFi: not connected, reconnect");
          connect();
        }
      }
    } else {
      tpl_config.wifi_manager_shutdown = true;
    }
    vTaskDelay(xDelay);
  }
}

void tpl_wifi_setup(bool verbose, bool waitOTA, gpio_num_t ledPin) {
  WiFi.setSleep(false);  // Disable WiFi power saving to reduce latency

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
    } else {
      WiFi.disconnect(/*wifioff=*/true);
    }
  }
  if (need_connect) {
    if (verbose) {
      Serial.println("WiFi connected");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
    }

    connect();
  }
  if (verbose) {
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  }

  if (MDNS.begin(HOSTNAME)) {
    Serial.println("MDNS responder started");
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("ws", "tcp", 81);
  }

  // Port defaults to 3232
  ArduinoOTA.setPort(3232);

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
          tpl_config.ota_ongoing = true;
        })
        .onEnd([]() {
          Serial.println("\nEnd");
          tpl_config.ota_ongoing = false;
          // delay to allow OTA response to be sent before reboot
          delay(500);
        })
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
  } else {
    ArduinoOTA.onStart([]() { tpl_config.ota_ongoing = true; }).onEnd([]() {
      tpl_config.ota_ongoing = false;
      // delay to allow OTA response to be sent before reboot
      delay(500);
    });
  }

  ArduinoOTA.begin();

  Serial.print("Retrieving time: ");
  configTime(0, 0, "pool.ntp.org");  // get UTC time via NTP
  time_t now = time(nullptr);
  while (now < 24 * 3600) {
    Serial.print(".");
    delay(100);
    now = time(nullptr);
  }

  setenv("TZ", "CET-1CEST,M3.5.0/2:00,M10.5.0/3:00", 1);
  tzset();

  char strftime_buf[64];
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  strftime(strftime_buf, sizeof(strftime_buf), "%d.%m.%y, %H:%M ", &timeinfo);
  Serial.println(strftime_buf);

  xTaskCreatePinnedToCore(TaskWifiManager, "WiFi_Manager", 2688, NULL, 0,
                          &tpl_tasks.task_wifi_manager, CORE_0);
  esp_task_wdt_add(tpl_tasks.task_wifi_manager);

  if (waitOTA) {
    // Wait OTA
    if (ledPin != 255) {
      pinMode(ledPin, OUTPUT);
    }
    uint32_t till = millis() + 10000;
    while ((millis() < till) || tpl_config.ota_ongoing) {
      if (ledPin != 255) {
        digitalWrite(ledPin, digitalRead(ledPin) == HIGH ? LOW : HIGH);
      }
      if (tpl_config.ota_ongoing) {
        delay(100);
      } else {
        delay(200);
      }
    }
    if (ledPin != 255) {
      digitalWrite(ledPin, HIGH);
    }
  }
}
