#include <mcp2515.h>
#include <AltSoftSerial.h>
#include "haus_can.h"

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
// Input buffer for serial data.  The meter's raw telegram is up to 256 bytes
// (258 on the wire once the 2 length bytes are added).  The old page stopped
// at 255, cutting off the second CRC byte, so every message failed its
// checksum on the receiving side.  Two pages so the next telegram can be
// received while the previous one is still being put on the CAN bus.
#define SML_PAGE 320       // bytes per page, > max telegram
#define SML_MAX_BYTES 256  // raw telegram, excludes the 2 CAN length bytes

uint32_t last_millis = 0;
uint32_t last_send = 0;
uint8_t input_buffer[2][SML_PAGE];
uint8_t in_page = 0;
uint16_t in_len = 0;
uint8_t send_page = 0;
uint16_t send_ptr = 0;
uint16_t send_len = 0;
uint16_t can_id;
uint16_t base_id = CAN_ID_STROMZAEHLER_INFO_BASIS;
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
    base_id = (base_id == CAN_ID_STROMZAEHLER_INFO_BASIS)
                  ? CAN_ID_STROMZAEHLER_INFO_ALT
                  : CAN_ID_STROMZAEHLER_INFO_BASIS;
  }

  if ((send_len > 0) && !can_msg_prepared) {
    canMsg.can_id = can_id++;
    uint8_t i = 0;
    if (send_ptr == 0) {
      canMsg.data[0] = send_len & 255;
      canMsg.data[1] = send_len >> 8;
      i = 2;
    }
    for (; (i < 8) && (send_len > 0); i++, send_ptr++, send_len--) {
      canMsg.data[i] = input_buffer[send_page][send_ptr];
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

  if (altser.available() > 0) {
    byte b = altser.read();
    last_millis = ms;
    // append to the active input page
    if (in_len < SML_MAX_BYTES) {
      input_buffer[in_page][in_len++] = b;
    }
  } else if (in_len != 0) {
    if (dt > 10) {
      // end telegram by timeout: hand the page over to the sender and make
      // the other page the input page
      send_page = in_page;
      send_ptr = 0;
      send_len = in_len;
      in_page ^= 1;
      in_len = 0;
      can_id = base_id;
      base_id = (base_id == CAN_ID_STROMZAEHLER_INFO_BASIS)
                    ? CAN_ID_STROMZAEHLER_INFO_ALT
                    : CAN_ID_STROMZAEHLER_INFO_BASIS;
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
