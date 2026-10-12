/**
 * @file tpl_matter.h
 * @brief Matter protocol integration for ESP32 template
 *
 * Prerequisites:
 * - ESP32 Arduino core 3.x with Matter support
 * - Build flags: -DCHIP_HAVE_CONFIG_H -DUSE_TPL_MATTER
 * - Partition scheme: min_spiffs.csv
 *
 * For unique pairing codes per project:
 * 1. Run: python tools/generate_matter_pairing.py <project_name>
 * 2. Add build flag: -DUSE_TPL_MATTER_CUSTOM_PAIRING
 */

#pragma once

#ifdef USE_TPL_MATTER

#include <Arduino.h>
#include <Matter.h>

#define TPL_MATTER_STANDALONE 0
#define TPL_MATTER_COEXIST    1

extern struct tpl_matter_state_s {
  volatile bool commissioned;
  volatile bool connected;
  volatile bool wifi_connected;
  char manual_pairing_code[32];
  char qr_code_url[128];
  uint8_t recovery_tier;
  uint32_t last_ok_ms;
} tpl_matter_state;

extern TaskHandle_t task_matter;

/**
 * @brief Initialize Matter with WiFi
 * @param mode TPL_MATTER_STANDALONE (connects WiFi) or TPL_MATTER_COEXIST (waits for tpl_wifi)
 */
void tpl_matter_setup(int mode = TPL_MATTER_STANDALONE);

/**
 * @brief Wait for Matter commissioning
 * @param timeout_ms Timeout (0 = forever)
 * @return true if commissioned
 */
bool tpl_matter_wait_commissioned(uint32_t timeout_ms = 0);

/**
 * @brief Decommission the Matter device
 */
void tpl_matter_decommission();

inline bool tpl_matter_is_commissioned() {
  return tpl_matter_state.commissioned;
}

inline bool tpl_matter_is_connected() {
  return tpl_matter_state.connected;
}

inline const char* tpl_matter_get_pairing_code() {
  return tpl_matter_state.manual_pairing_code;
}

inline const char* tpl_matter_get_qr_url() {
  return tpl_matter_state.qr_code_url;
}

#endif
