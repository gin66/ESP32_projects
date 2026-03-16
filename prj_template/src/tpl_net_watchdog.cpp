#include "tpl_net_watchdog.h"

#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_task_wdt.h>
#include <stdint.h>

#include "tpl_system.h"

#ifdef NET_WATCHDOG

static WiFiUDP ntpUDP;
static WiFiUDP dnsUDP;
static uint8_t ntp_consecutive_fails = 0;

static const int NTP_TIMEOUT_MS = 1000;
static const int NTP_ATTEMPTS = 3;
static const int NTP_SUCCESS_THRESHOLD = 2;
static const int DNS_TIMEOUT_MS = 1000;
static const int DNS_ATTEMPTS = 3;

static bool checkNTP() {
  uint8_t packet[48];
  memset(packet, 0, 48);
  packet[0] = 0b11100011;

  int successes = 0;
  for (int i = 0; i < NTP_ATTEMPTS; i++) {
    ntpUDP.begin(0);

    IPAddress ip;
    if (WiFi.hostByName(NET_WATCHDOG, ip)) {
      ntpUDP.beginPacket(ip, 123);
      ntpUDP.write(packet, 48);
      ntpUDP.endPacket();

      uint32_t start = millis();
      while (millis() - start < NTP_TIMEOUT_MS) {
        if (ntpUDP.parsePacket() >= 48) {
          successes++;
          break;
        }
        delay(5);
      }
    }
    ntpUDP.stop();

    if (successes >= NTP_SUCCESS_THRESHOLD) break;
    delay(50);
  }
  return successes >= NTP_SUCCESS_THRESHOLD;
}

static bool checkDNS() {
  uint8_t query[] = {0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
                     0x00, 0x00, 0x00, 0x00, 0x04, 't', 'e', 's', 't', 0x00, 0x01, 0x01};

  int successes = 0;
  for (int i = 0; i < DNS_ATTEMPTS; i++) {
    dnsUDP.begin(0);

    IPAddress ip;
    if (WiFi.hostByName(NET_WATCHDOG, ip)) {
      dnsUDP.beginPacket(ip, 53);
      dnsUDP.write(query, sizeof(query));
      dnsUDP.endPacket();

      uint32_t start = millis();
      while (millis() - start < DNS_TIMEOUT_MS) {
        if (dnsUDP.parsePacket() > 0) {
          successes++;
          break;
        }
        delay(5);
      }
    }
    dnsUDP.stop();

    if (successes >= 1) break;
    delay(50);
  }
  return successes >= 1;
}

uint16_t tpl_fail = 0;

void TaskWatchdog(void* pvParameters) {
  const TickType_t xDelay = 1000 / portTICK_PERIOD_MS;
  uint16_t log_counter = 0;

  for (;;) {
    esp_task_wdt_reset();

    if (tpl_config.wifi_recovery_tier > 0) {
      if (++log_counter >= 10) {
        log_counter = 0;
        Serial.printf("NetWatch: skipped (tier=%d)\n", tpl_config.wifi_recovery_tier);
      }
      vTaskDelay(xDelay);
      continue;
    }

    IPAddress ip = WiFi.localIP();
    bool has_valid_ip = ip[0] != 0 || ip[1] != 0 || ip[2] != 0 || ip[3] != 0;

    bool ntp_ok = false;
    bool dns_ok = false;
    bool success = false;

    if (has_valid_ip) {
      ntp_ok = checkNTP();
      if (ntp_ok) {
        ntp_consecutive_fails = 0;
        success = true;
      } else {
        ntp_consecutive_fails++;
        if (ntp_consecutive_fails >= 3) {
          dns_ok = checkDNS();
          if (dns_ok) {
            success = true;
          }
        }
      }
    }

    if (++log_counter >= 10) {
      log_counter = 0;
      Serial.printf("NetWatch: NTP=%s DNS=%s IP=%s fail=%d tier=%d\n",
        ntp_ok ? "OK" : "FAIL", dns_ok ? "OK" : (ntp_ok ? "-" : "FAIL"),
        ip.toString().c_str(), tpl_fail, tpl_config.wifi_recovery_tier);
    }

    if (success) {
      tpl_fail = 0;
      tpl_config.net_last_ok_ms = millis();
    } else {
      tpl_fail++;
      if (!has_valid_ip) {
        Serial.printf("NetWatch: no valid IP, fail=%d\n", tpl_fail);
      } else if (ntp_consecutive_fails >= 3) {
        Serial.printf("NetWatch: NTP+DNS failed, fail=%d\n", tpl_fail);
      } else {
        Serial.printf("NetWatch: NTP failed (cnt=%d), fail=%d\n", ntp_consecutive_fails, tpl_fail);
      }

      if (tpl_fail >= 300) {
        Serial.println("NetWatch: 5min timeout, restarting");
        delay(100);
        ESP.restart();
      }
    }

    vTaskDelay(xDelay);
  }
}

bool tpl_net_watchdog_setup() {
  BaseType_t rc;

  rc = xTaskCreatePinnedToCore(TaskWatchdog, "Net Watchdog", 4096, NULL, 0,
                               &tpl_tasks.task_net_watchdog, CORE_0);
  if (rc == pdPASS) {
    esp_task_wdt_add(tpl_tasks.task_net_watchdog);
  }
  return (rc == pdPASS);
}

#elif NO_NET_WATCHDOG
#else
#error "Need to define either NET_WATCHDOG=<ip> or NO_NET_WATCHDOG"
#endif
