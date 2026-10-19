#include "tpl_wifi.h"
#include "tpl_debug_log.h"

#include <ArduinoOTA.h>

#ifdef IS_WAVESHARE_LCD7
#include "tpl_ws7.h"
#endif
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

/* How long setup() waits for NTP before giving up and continuing. Without an
 * IP address it can never succeed, so this must be bounded. */
#ifndef TPL_NTP_WAIT_MS
#define TPL_NTP_WAIT_MS 10000UL
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

RTC_DATA_ATTR int rssi_roam_reconnect_count = 0;
int tpl_wifi_get_rssi_roam_count() { return rssi_roam_reconnect_count; }

/* Manager-side reconnect must never block. WiFiMulti::run() performs a full
 * scan and then waits up to its connect timeout with no opportunity to feed the
 * task watchdog the manager is subscribed to; with several configured networks
 * that exceeds the default timeout and panics. Rotate through the configured
 * networks with the non-blocking WiFi.begin() instead, one attempt every 10 s. */
static uint8_t s_retry_net = 0;
static uint32_t s_next_retry_ms = 0;
static uint32_t s_connected_since_ms = 0;

static void wifi_retry_step() {
  const uint32_t now = millis();
  if ((int32_t)(now - s_next_retry_ms) < 0) return;
  s_next_retry_ms = now + 10000;

  const struct net_s *net = &nets[s_retry_net];
  if (!net->ssid) {
    s_retry_net = 0;
    net = &nets[0];
  }
  if (!net->ssid) return;

  TPL_LOGI("wifi", "WiFi: retry begin %s\n", net->ssid);
  WiFi.begin(net->ssid, net->passwd);
  s_retry_net++;
}

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
        s_connected_since_ms = 0;
        s_retry_net = 0;
        s_next_retry_ms = 0;

        int rssi = WiFi.RSSI();
        if (rssi < RSSI_RECONNECT_THRESHOLD && !tpl_config.ota_ongoing) {
          if (weak_rssi_since_ms == 0) weak_rssi_since_ms = millis();
          if ((millis() - weak_rssi_since_ms) / 1000 >= RSSI_RECONNECT_SUSTAINED_SEC) {
            uint8_t* bssid = WiFi.BSSID();
            TPL_LOGI("wifi", "WiFi: weak signal %d dBm for %" PRIu32 "s (BSSID %02X:%02X:%02X:%02X:%02X:%02X ch%d), reconnecting (#%d)\n",
                          rssi, (millis() - weak_rssi_since_ms) / 1000,
                          bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
                          (int)WiFi.channel(), rssi_roam_reconnect_count + 1);
            esp_task_wdt_reset();
            WiFi.disconnect(true);
            vTaskDelay(100 / portTICK_PERIOD_MS);
            esp_task_wdt_reset();
            s_retry_net = 0;
            s_next_retry_ms = 0;
            wifi_retry_step();
            rssi_roam_reconnect_count++;
            weak_rssi_since_ms = 0;
          }
        } else {
          weak_rssi_since_ms = 0;
        }

        if (tpl_config.wifi_recovery_tier != 0) {
          TPL_LOGI("wifi", "WiFi: recovered, tier=%d->0\n",
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
        // ArduinoOTA.handle() is blocking — it receives and flashes firmware
        // chunks internally, so the WDT must be fed in onProgress callback
        ArduinoOTA.handle();
      } else {
        weak_rssi_since_ms = 0;
        uint32_t secs_disconnected = (millis() - last_ok_ms) / 1000;

        /* WL_CONNECTED before DHCP finishes is normal, so do not tear the
         * association down immediately: that is what starved DHCP and made the
         * manager reconnect in a loop. Give it 15 s, then start over. */
        if (!has_valid_ip && status == WL_CONNECTED) {
          if (s_connected_since_ms == 0) {
            s_connected_since_ms = millis();
          } else if (millis() - s_connected_since_ms > 15000) {
            TPL_LOGI("wifi", "WiFi: DHCP timeout, force disconnect");
            WiFi.disconnect(true);
            s_connected_since_ms = 0;
            s_next_retry_ms = 0;
            vTaskDelay(100 / portTICK_PERIOD_MS);
          }
        } else {
          s_connected_since_ms = 0;
        }

        if (secs_disconnected > 120 && tpl_config.wifi_recovery_tier < 2) {
          TPL_LOGI("wifi", "WiFi: Tier 2 - stack reset (disconnected %" PRIu32 "s)\n",
                        secs_disconnected);
          WATCH(21);
          tpl_config.wifi_recovery_tier = 2;
          WiFi.mode(WIFI_OFF);
          esp_task_wdt_reset();
          vTaskDelay(1000 / portTICK_PERIOD_MS);
          WiFi.mode(WIFI_STA);
          esp_task_wdt_reset();
          s_retry_net = 0;
          s_next_retry_ms = 0;
          wifi_retry_step();
        } else if (secs_disconnected > 30 &&
                   tpl_config.wifi_recovery_tier < 1) {
          TPL_LOGI("wifi", "WiFi: Tier 1 - reconnect (disconnected %" PRIu32 "s)\n",
                        secs_disconnected);
          WATCH(11);
          tpl_config.wifi_recovery_tier = 1;
          esp_task_wdt_reset();
          WiFi.reconnect();
        } else if (tpl_config.wifi_recovery_tier == 0) {
          esp_task_wdt_reset();
          wifi_retry_step();
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
      TPL_LOGI("wifi", "Try last connected network: %s", nets[last_connected_network].ssid);
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
    connect();
  }
  // Wait for DHCP to assign an IP address before proceeding.
  // WiFi.waitForConnectResult() returns WL_CONNECTED before DHCP completes,
  // so localIP() is still 0.0.0.0 at this point.
  unsigned long dhcp_start = millis();
  while (WiFi.localIP() == INADDR_NONE && (millis() - dhcp_start) < 5000UL) {
    Serial.print(".");
    delay(100);
  }
  Serial.println();

  if (verbose) {
    TPL_LOGI("wifi", "WiFi connected");
    TPL_LOGI("wifi", "IP address: %s", WiFi.localIP().toString().c_str());
    TPL_LOGI("wifi", "MDNS responder started");
    MDNS.begin(HOSTNAME);
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
          TPL_LOGI("wifi", "Start updating %s", type.c_str());
          tpl_config.ota_ongoing = true;
          /* The RGB DMA keeps scanning PSRAM while flash writes disable the
           * cache: blank the panel instead of showing garbage. */
#ifdef IS_WAVESHARE_LCD7
          tpl_ws7_backlight(false);
#endif
        })
        .onEnd([]() {
          TPL_LOGI("wifi", "\nEnd");
          tpl_config.ota_ongoing = false;
          // delay to allow OTA response to be sent before reboot
          delay(500);
        })
        .onProgress([](unsigned int progress, unsigned int total) {
          esp_task_wdt_reset();
          TPL_LOGI("wifi", "Progress: %u%%\r", (progress / (total / 100)));
        })
        .onError([](ota_error_t error) {
          TPL_LOGI("wifi", "Error[%u]: ", error);
#ifdef IS_WAVESHARE_LCD7
          tpl_ws7_backlight(true);
#endif
          if (error == OTA_AUTH_ERROR)
            TPL_LOGI("wifi", "Auth Failed");
          else if (error == OTA_BEGIN_ERROR)
            TPL_LOGI("wifi", "Begin Failed");
          else if (error == OTA_CONNECT_ERROR)
            TPL_LOGI("wifi", "Connect Failed");
          else if (error == OTA_RECEIVE_ERROR)
            TPL_LOGI("wifi", "Receive Failed");
          else if (error == OTA_END_ERROR)
            TPL_LOGI("wifi", "End Failed");
        });
  } else {
    ArduinoOTA.onStart([]() {
      tpl_config.ota_ongoing = true;
#ifdef IS_WAVESHARE_LCD7
      tpl_ws7_backlight(false);
#endif
    }).onEnd([]() {
      tpl_config.ota_ongoing = false;
      delay(500);
    }).onProgress([](unsigned int, unsigned int) {
      esp_task_wdt_reset();
    });
  }

  ArduinoOTA.begin();

  /* Start the manager task first: it is what reconnects, and the NTP wait
   * below must not be able to starve it. */
  xTaskCreatePinnedToCore(TaskWifiManager, "WiFi_Manager", 6144, NULL, 0,
                          &tpl_tasks.task_wifi_manager, CORE_0);
  esp_task_wdt_add(tpl_tasks.task_wifi_manager);

  /* Wait for NTP, but only for a bounded time. Without an IP address SNTP can
   * never succeed, and blocking here forever meant a board that missed its DHCP
   * lease never reached setup() and never recovered — the manager task had not
   * even been started yet. SNTP keeps running in the background, so once WiFi is
   * up the clock is set without any further action. */
  TPL_LOGI("wifi", "Retrieving time");
  /* configTzTime(), not configTime(): configTime(gmt=0) calls setTimeZone(0,0)
   * and overwrites TZ, so the clock would silently stay in UTC. This sets both
   * the SNTP servers and the POSIX timezone (CET/CEST, Europe/Berlin). */
  configTzTime("CET-1CEST,M3.5.0/2:00,M10.5.0/3:00", "pool.ntp.org");
  const unsigned long ntp_deadline = millis() + TPL_NTP_WAIT_MS;
  time_t now = time(nullptr);
  while (now < 24 * 3600 && static_cast<long>(millis() - ntp_deadline) < 0) {
    Serial.print(".");
    delay(100);
    now = time(nullptr);
  }
  Serial.println();

  if (now < 24 * 3600) {
    TPL_LOGW("wifi", "no NTP time yet (no IP?); continuing without a valid clock. "
        "It will sync as soon as WiFi is up.");
  } else {
    char strftime_buf[64];
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%d.%m.%y, %H:%M ", &timeinfo);
    TPL_LOGI("wifi", "%s", strftime_buf);
  }

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
