#include "hm310t.h"
#include "template.h"

// WebSocket publish: called every 100 ms by tpl_websocket task when clients connected.
// All values in raw register units; browser converts for display:
//   v_out / 100.0  => volts
//   i_out / 1000.0 => amps
//   v_set / 100.0  => volts setpoint
//   i_set / 1000.0 => amps setpoint
void publish_func(DynamicJsonDocument *json) {
  if (!hm310t_state.valid) {
    (*json)["hm_valid"] = false;
    return;
  }
  (*json)["hm_valid"]  = true;
  (*json)["output"]    = hm310t_state.output;
  (*json)["v_out"]     = hm310t_state.v_out;   // 0.01 V
  (*json)["i_out"]     = hm310t_state.i_out;   // 0.001 A
  (*json)["p_out"]     = hm310t_state.p_out;   // 32-bit raw
  (*json)["v_set"]     = hm310t_state.v_set;   // 0.01 V
  (*json)["i_set"]     = hm310t_state.i_set;   // 0.001 A
}

// WebSocket commands (JSON keys):
//   {"v_set": 1200}   -> set voltage to 12.00 V
//   {"i_set": 500}    -> set current to 0.500 A
//   {"output": 1}     -> turn output on  (0 = off)
void process_func(DynamicJsonDocument *json) {
  if (json->containsKey("v_set")) {
    hm310t_cmd.new_v     = (*json)["v_set"];
    hm310t_cmd.pending_v = true;
  }
  if (json->containsKey("i_set")) {
    hm310t_cmd.new_i     = (*json)["i_set"];
    hm310t_cmd.pending_i = true;
  }
  if (json->containsKey("output")) {
    hm310t_cmd.new_output     = (*json)["output"];
    hm310t_cmd.pending_output = true;
  }
}

void setup() {
  tpl_system_setup(0);

  Serial.begin(115200);
  Serial.setDebugOutput(false);

  tpl_wifi_setup(true, true, (gpio_num_t)2 /*onboard LED*/);
  tpl_webserver_setup();
  tpl_websocket_setup(publish_func, process_func);
  tpl_net_watchdog_setup();
  tpl_command_setup(NULL);

  hm310t_setup();

  Serial.println("Setup done.");
}

void loop() { taskYIELD(); }
