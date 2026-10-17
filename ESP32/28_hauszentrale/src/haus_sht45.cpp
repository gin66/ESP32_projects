/**
 * @file haus_sht45.cpp
 * @brief Sensirion SHT45 driver: high-precision T/RH, non-blocking polling.
 *
 * Protocol (SHT4x datasheet):
 *   0xFD  measure high precision, max 8.3 ms conversion
 *   0x94  soft reset, 1 ms
 *   reply 6 bytes: T_msb T_lsb T_crc RH_msb RH_lsb RH_crc
 *   CRC-8, polynomial 0x31, init 0xFF, over each 2-byte word
 *
 * Every transaction takes the bus-wide I2C mutex (N13) with a bounded timeout
 * so a stuck sensor cannot stall the touch path.
 */

#include "haus_sht45.h"

#include "tpl_debug_log.h"
#include "tpl_ws7.h"

#include <Arduino.h>
#include <Wire.h>

#define SHT45_CMD_MEASURE_HIGH 0xFD
#define SHT45_CMD_SOFT_RESET 0x94
#define SHT45_I2C_TIMEOUT_MS 20
#define SHT45_REPROBE_MS 5000

static bool s_present = false;
static bool s_valid = false;
static bool s_pending = false;
static int16_t s_temp_c100 = 0;
static uint16_t s_hum_c100 = 0;
static uint32_t s_last_probe_ms = 0;
static uint32_t s_last_fault_log_ms = 0;

static uint8_t sht45_crc8(const uint8_t *data, size_t len) {
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; bit++) {
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
    }
  }
  return crc;
}

static bool sht45_write_cmd(uint8_t cmd) {
  Wire.beginTransmission(HAUS_SHT45_ADDR);
  Wire.write(cmd);
  return Wire.endTransmission() == 0;
}

static bool sht45_probe(void) {
  Wire.beginTransmission(HAUS_SHT45_ADDR);
  return Wire.endTransmission() == 0;
}

bool haus_sht45_init(void) {
  Wire.begin(TPL_WS7_I2C_SDA, TPL_WS7_I2C_SCL, 400000);

  if (tpl_ws7_i2c_lock(SHT45_I2C_TIMEOUT_MS) != ESP_OK) {
    TPL_LOGW("climate", "SHT45: I2C busy during init");
    return false;
  }
  const bool ack = sht45_probe();
  bool reset_ok = false;
  if (ack) {
    reset_ok = sht45_write_cmd(SHT45_CMD_SOFT_RESET);
  }
  tpl_ws7_i2c_unlock();

  if (!ack) {
    TPL_LOGW("climate", "SHT45: no ACK at 0x%02X", HAUS_SHT45_ADDR);
    s_present = false;
    return false;
  }
  if (!reset_ok) {
    TPL_LOGW("climate", "SHT45: soft reset failed");
    s_present = false;
    return false;
  }

  delay(2);
  s_present = true;
  s_pending = false;
  s_valid = false;
  TPL_LOGI("climate", "SHT45 ready at 0x%02X", HAUS_SHT45_ADDR);
  return true;
}

bool haus_sht45_poll(int16_t *temp_c100, uint16_t *hum_c100) {
  if (!temp_c100 || !hum_c100)
    return false;

  const uint32_t now = millis();

  if (!s_present) {
    if (now - s_last_probe_ms < SHT45_REPROBE_MS) {
      *temp_c100 = s_temp_c100;
      *hum_c100 = s_hum_c100;
      return false;
    }
    s_last_probe_ms = now;
    if (tpl_ws7_i2c_lock(SHT45_I2C_TIMEOUT_MS) != ESP_OK)
      return false;
    const bool ack = sht45_probe();
    tpl_ws7_i2c_unlock();
    if (!ack) {
      s_valid = false;
      *temp_c100 = s_temp_c100;
      *hum_c100 = s_hum_c100;
      return false;
    }
    s_present = true;
    s_pending = false;
    TPL_LOGI("climate", "SHT45 reconnected at 0x%02X", HAUS_SHT45_ADDR);
  }

  if (tpl_ws7_i2c_lock(SHT45_I2C_TIMEOUT_MS) != ESP_OK) {
    *temp_c100 = s_temp_c100;
    *hum_c100 = s_hum_c100;
    return s_valid;
  }

  if (s_pending) {
    uint8_t data[6] = {0};
    const int got = Wire.requestFrom((int)HAUS_SHT45_ADDR, (size_t)6);
    for (int i = 0; i < got && i < 6; i++)
      data[i] = (uint8_t)Wire.read();

    if (got == 6 && sht45_crc8(data, 2) == data[2] &&
        sht45_crc8(data + 3, 2) == data[5]) {
      const uint16_t raw_t = ((uint16_t)data[0] << 8) | data[1];
      const uint16_t raw_rh = ((uint16_t)data[3] << 8) | data[4];

      s_temp_c100 =
          (int16_t)(-4500 +
                    (int32_t)(((int32_t)17500 * raw_t + 32767) / 65535));
      int32_t rh = -600 + (int32_t)(((int32_t)12500 * raw_rh + 32767) / 65535);
      if (rh < 0)
        rh = 0;
      if (rh > 10000)
        rh = 10000;
      s_hum_c100 = (uint16_t)rh;
      s_valid = true;
    } else {
      s_valid = false;
      if (now - s_last_fault_log_ms >= 10000) {
        s_last_fault_log_ms = now;
        TPL_LOGW("climate", "SHT45: short read (%d bytes) or CRC error", got);
      }
    }
    s_pending = false;
  }

  /* Start the next measurement. The next poll is a full second away, well
   * past the 8.3 ms conversion, so no busy-wait is needed. */
  if (!sht45_write_cmd(SHT45_CMD_MEASURE_HIGH)) {
    s_present = false;
    s_pending = false;
    s_valid = false;
    s_last_probe_ms = now;
    if (now - s_last_fault_log_ms >= 10000) {
      s_last_fault_log_ms = now;
      TPL_LOGW("climate", "SHT45: measure command NACK");
    }
  } else {
    s_pending = true;
  }

  tpl_ws7_i2c_unlock();

  *temp_c100 = s_temp_c100;
  *hum_c100 = s_hum_c100;
  return s_valid;
}

bool haus_sht45_present(void) { return s_present; }
