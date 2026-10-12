#include <mcp2515.h>
#include <AltSoftSerial.h>

MCP2515 mcp2515(10);  // using pin 10 for CAN CS
#ifdef SENDER
struct can_frame canMsg;
#endif
#ifdef RECEIVER
struct can_frame canMsg;
#endif

AltSoftSerial altser;

void setup() {
  Serial.begin(115200);
  altser.begin(9600);

  MCP2515::ERROR res = mcp2515.reset();
  if (res == MCP2515::ERROR_OK) {
    mcp2515.setBitrate(CAN_125KBPS, MCP_8MHZ);
    mcp2515.setNormalMode();
  } else {
    Serial.println("Error initializing mcp2515");
  }
}  // setup

#ifdef SENDER
// input buffer for serial data
uint32_t last_millis = 0;
uint32_t last_send = 0;
uint8_t input_buffer[2 * 256];
uint16_t in_offset = 0;
uint16_t send_ptr = 0;
uint16_t send_len = 0;
uint16_t can_id;
uint16_t base_id = 0x600;
bool can_msg_prepared = false;

void loop() {
  uint32_t ms = millis();
  uint32_t dt = ms - last_millis;
  if (dt > 1000) {
    // send alive message
    canMsg.can_id = base_id;
    canMsg.can_dlc = 0;
    can_msg_prepared = true;
    last_millis = ms;
    base_id ^= 0x100;
  }

  if ((send_len > 0) && !can_msg_prepared) {
    canMsg.can_id = can_id++;
    uint8_t i = 0;
    if ((send_ptr & 255) == 0) {
      canMsg.data[0] = send_len & 255;
      canMsg.data[1] = send_len >> 8;
      i = 2;
    }
    for (; (i < 8) && (send_len > 0); i++, send_ptr++, send_len--) {
      canMsg.data[i] = input_buffer[send_ptr];
    }
    canMsg.can_dlc = i;
    can_msg_prepared = true;
  }

  if (can_msg_prepared) {
    uint32_t send_dt = ms - last_send;
    if (send_dt > 10) {  // one message per 10 ms
      MCP2515::ERROR res = mcp2515.sendMessage(&canMsg);
      if (res == MCP2515::ERROR_OK) {
        can_msg_prepared = false;
        last_send = ms;
      }
    }
  }

  //  if (Serial.available() > 0) {
  //    byte b = Serial.read();
  if (altser.available() > 0) {
    byte b = altser.read();
    last_millis = ms;
    // append to buffer
    if ((in_offset & 255) != 255) {
      input_buffer[in_offset++] = b;
    }
  } else if ((in_offset & 255) != 0) {
    if (dt > 10) {
      // end telegram by timeout
      if (in_offset < 256) {
        send_ptr = 0;
        send_len = in_offset;
        in_offset = 256;
      } else {
        send_ptr = 256;
        send_len = in_offset - 256;
        in_offset = 0;
      }
      can_id = base_id;
      base_id ^= 0x100;
    }
  }
}  // loop
#endif

#ifdef RECEIVER
void loop() {
  if (mcp2515.readMessage(&canMsg) == MCP2515::ERROR_OK) {
    Serial.print(canMsg.can_id, HEX);  // print ID
    Serial.print(" ");
    Serial.print(canMsg.can_dlc, HEX);  // print DLC
    Serial.print(" ");

    for (int i = 0; i < canMsg.can_dlc; i++) {  // print the data
      Serial.print(canMsg.data[i], HEX);
      Serial.print(" ");
    }

    Serial.println();
  }
}  // loop
#endif
