#pragma once
#include <Arduino.h>

// HM310T connects via its USB port to the MAX3421E USB host controller.
// MAX3421E wiring to ESP32 VSPI:
//   SCK  -> GPIO18   MISO -> GPIO19
//   MOSI -> GPIO23   SS   -> GPIO5
//   INT  -> GPIO4

#define HM310T_ADDR 0x01

// Register map (Modbus FC03 read, FC06 write single)
// Units: voltage 0.01 V, current 0.001 A, power raw 32-bit
#define HM310T_REG_OUTPUT  0x0001  // Output enable: 0=off, 1=on  (R/W)
#define HM310T_REG_VOUT    0x0010  // Measured voltage  [0.01 V]  (R)
#define HM310T_REG_IOUT    0x0011  // Measured current  [0.001 A] (R)
#define HM310T_REG_POUT_HI 0x0012  // Measured power high word    (R)
#define HM310T_REG_POUT_LO 0x0013  // Measured power low word     (R)
#define HM310T_REG_VSET    0x0030  // Voltage setpoint  [0.01 V]  (R/W)
#define HM310T_REG_ISET    0x0031  // Current setpoint  [0.001 A] (R/W)

// Device needs ~1 s to settle after a write
#define HM310T_WRITE_DELAY_MS 1000

struct hm310t_state_s {
  uint16_t output;
  uint16_t v_out;
  uint16_t i_out;
  uint32_t p_out;
  uint16_t v_set;
  uint16_t i_set;
  bool     valid;
};
extern volatile hm310t_state_s hm310t_state;

struct hm310t_cmd_s {
  volatile bool     pending_v;
  volatile bool     pending_i;
  volatile bool     pending_output;
  volatile uint16_t new_v;
  volatile uint16_t new_i;
  volatile uint16_t new_output;
};
extern volatile hm310t_cmd_s hm310t_cmd;

void hm310t_setup();
