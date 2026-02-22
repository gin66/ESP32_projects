#include "hm310t.h"

#include "ch340.h"
#include "tpl_system.h"

volatile hm310t_state_s hm310t_state = {};
volatile hm310t_cmd_s   hm310t_cmd   = {};

// MAX3421E on VSPI (SCK=18, MISO=19, MOSI=23, SS=5), INT=4
static USB   Usb;
static CH340 ch340(&Usb);

// ---------------------------------------------------------------------------
// Modbus RTU helpers
// ---------------------------------------------------------------------------

static uint16_t crc16(const uint8_t *buf, uint16_t len) {
  uint16_t crc = 0xFFFF;
  for (uint16_t i = 0; i < len; i++) {
    crc ^= buf[i];
    for (uint8_t j = 0; j < 8; j++)
      crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
  }
  return crc;
}

// Drain any pending RX bytes from CH340
static void flush_rx() {
  uint8_t tmp[32];
  uint16_t got;
  do {
    got = sizeof(tmp);
    ch340.read(tmp, sizeof(tmp), &got);
    if (got) Usb.Task();
  } while (got > 0);
}

// FC03: read `count` contiguous holding registers starting at `reg`.
static bool read_regs(uint16_t reg, uint16_t count, uint16_t *out) {
  flush_rx();

  uint8_t req[8];
  req[0] = HM310T_ADDR; req[1] = 0x03;
  req[2] = reg >> 8;    req[3] = reg & 0xFF;
  req[4] = count >> 8;  req[5] = count & 0xFF;
  uint16_t crc = crc16(req, 6);
  req[6] = crc & 0xFF;  req[7] = crc >> 8;
  ch340.write(req, 8);

  // Accumulate response bytes within timeout
  const uint16_t rlen = 3 + count * 2 + 2;
  uint8_t resp[64];
  uint16_t rx = 0;
  uint32_t deadline = millis() + 400;
  while (rx < rlen && millis() < deadline) {
    uint8_t tmp[32];
    uint16_t got = min((uint16_t)(rlen - rx), (uint16_t)sizeof(tmp));
    if (ch340.read(tmp, got, &got) == 0 && got > 0) {
      memcpy(resp + rx, tmp, got);
      rx += got;
    } else {
      Usb.Task();
      vTaskDelay(1 / portTICK_PERIOD_MS);
    }
  }

  if (rx < rlen) {
    Serial.printf("HM310T: read_regs(0x%04X,%d) timeout rx=%d\n", reg, count, rx);
    return false;
  }
  uint16_t rcrc = crc16(resp, rlen - 2);
  if (resp[rlen-2] != (rcrc & 0xFF) || resp[rlen-1] != (rcrc >> 8)) {
    Serial.printf("HM310T: CRC mismatch reg=0x%04X\n", reg);
    return false;
  }
  if (resp[0] != HM310T_ADDR || resp[1] != 0x03) return false;

  for (uint16_t i = 0; i < count; i++)
    out[i] = ((uint16_t)resp[3 + i*2] << 8) | resp[4 + i*2];
  return true;
}

// FC06: write single holding register. Device needs ~1 s to settle.
static bool write_reg(uint16_t reg, uint16_t val) {
  flush_rx();

  uint8_t req[8];
  req[0] = HM310T_ADDR; req[1] = 0x06;
  req[2] = reg >> 8;    req[3] = reg & 0xFF;
  req[4] = val >> 8;    req[5] = val & 0xFF;
  uint16_t crc = crc16(req, 6);
  req[6] = crc & 0xFF;  req[7] = crc >> 8;
  ch340.write(req, 8);

  // FC06 response is an echo of the request
  uint8_t resp[8];
  uint16_t rx = 0;
  uint32_t deadline = millis() + 400;
  while (rx < 8 && millis() < deadline) {
    uint8_t tmp[8];
    uint16_t got = 8 - rx;
    if (ch340.read(tmp, got, &got) == 0 && got > 0) {
      memcpy(resp + rx, tmp, got);
      rx += got;
    } else {
      Usb.Task();
      vTaskDelay(1 / portTICK_PERIOD_MS);
    }
  }
  if (rx < 8 || memcmp(req, resp, 8) != 0) {
    Serial.printf("HM310T: write_reg(0x%04X) failed rx=%d\n", reg, rx);
    return false;
  }

  // Allow device to settle; keep USB alive during the wait
  uint32_t settle_end = millis() + HM310T_WRITE_DELAY_MS;
  while (millis() < settle_end) {
    Usb.Task();
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
  return true;
}

// ---------------------------------------------------------------------------
// FreeRTOS task
// ---------------------------------------------------------------------------

static void TaskHM310T(void *) {
  Serial.println("HM310T: USB init");
  SPI.begin(18, 19, 23, 5);  // MAX3421E VSPI: SCK, MISO, MOSI, SS
  if (Usb.Init() == -1) {
    Serial.println("HM310T: MAX3421E not found — check wiring");
  }

  uint32_t last_poll_ms = 0;
  for (;;) {
    Usb.Task();

    if (!ch340.connected()) {
      vTaskDelay(10 / portTICK_PERIOD_MS);
      continue;
    }

    uint32_t now = millis();
    if (now - last_poll_ms < 500) {
      vTaskDelay(5 / portTICK_PERIOD_MS);
      continue;
    }
    last_poll_ms = now;

    WATCH(10);

    // --- Pending writes (one per cycle; each takes ~1 s settle) ---
    if (hm310t_cmd.pending_v) {
      hm310t_cmd.pending_v = false;
      Serial.printf("HM310T: set V=%d (%.2fV)\n", hm310t_cmd.new_v, hm310t_cmd.new_v * 0.01f);
      write_reg(HM310T_REG_VSET, hm310t_cmd.new_v);
    } else if (hm310t_cmd.pending_i) {
      hm310t_cmd.pending_i = false;
      Serial.printf("HM310T: set I=%d (%.3fA)\n", hm310t_cmd.new_i, hm310t_cmd.new_i * 0.001f);
      write_reg(HM310T_REG_ISET, hm310t_cmd.new_i);
    } else if (hm310t_cmd.pending_output) {
      hm310t_cmd.pending_output = false;
      Serial.printf("HM310T: set output=%d\n", hm310t_cmd.new_output);
      write_reg(HM310T_REG_OUTPUT, hm310t_cmd.new_output);
    }

    // --- Poll: output state (reg 0x0001, 1 register) ---
    uint16_t output_reg;
    bool ok1 = read_regs(HM310T_REG_OUTPUT, 1, &output_reg);

    // --- Poll: measurements (0x0010–0x0013, 4 registers) ---
    uint16_t meas[4];
    bool ok2 = read_regs(HM310T_REG_VOUT, 4, meas);

    // --- Poll: setpoints (0x0030–0x0031, 2 registers) ---
    uint16_t sp[2];
    bool ok3 = read_regs(HM310T_REG_VSET, 2, sp);

    if (ok1 && ok2 && ok3) {
      hm310t_state.output = output_reg;
      hm310t_state.v_out  = meas[0];
      hm310t_state.i_out  = meas[1];
      hm310t_state.p_out  = ((uint32_t)meas[2] << 16) | meas[3];
      hm310t_state.v_set  = sp[0];
      hm310t_state.i_set  = sp[1];
      hm310t_state.valid  = true;
      WATCH(11);
    } else {
      hm310t_state.valid = false;
    }
  }
}

void hm310t_setup() {
  xTaskCreatePinnedToCore(TaskHM310T, "hm310t", 4096, NULL, 1,
                          &tpl_tasks.task_app1, CORE_1);
  tpl_tasks.app_name1 = "hm310t";
}
