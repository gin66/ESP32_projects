#include <nRF24-SOLDERED.h>
#include <SoftwareSerial.h>

#include "nrf_modules.h"
#include "private_haus.h"

#define DEBUG

// V1.0 !!!!
// https://github.com/emakefun/rf-nano/blob/master/schematic/rf-nano_sch_v3.0.pdf
#define PIN_RF_CS 9   // D9 = Chip Select (CS) for nRF24L01+
#define PIN_RF_CE 10  // D10 = Chip Enable (CE) for nRF24L01+
// Pin 10 is PB2 - SS
// Pin 11 is PB3 - MOSI
// Pin 12 is PB4 - MISO
// Pin 13 is PB5 - SCK
#define RXD 14  /* A2 */
#define TXD 15  /* A1 */

// instantiate an object for the nRF24L01 transceiver
nRF24 radio(PIN_RF_CE, PIN_RF_CS);

Esmart3Command received;
Esmart3Payload payload;

SoftwareSerial Esmart3Serial(RXD, TXD);

void setup() {
  Serial.begin(115200);

  pinMode(RXD, INPUT);
  pinMode(TXD, OUTPUT);

  // Set the baud rate for the SoftwareSerial object
  Esmart3Serial.begin(9600);

  // initialize the transceiver on the SPI bus
  while (!radio.begin()) {
    Serial.println(F("radio hardware is not responding!!"));
    delay(1000);
    // hold in infinite loop
  }

  Serial.println(F("Esmart3 module"));

  // Set the PA Level low to try preventing power supply related problems
  // because these examples are likely run with nodes in close proximity to
  // each other.
  radio.setPALevel(RF24_PA_LOW);  // RF24_PA_MAX is default.

  radio.setDataRate(RF24_250KBPS);

  radio.setCRCLength(RF24_CRC_16);

  // to use ACK payloads, we need to enable dynamic payload lengths (for all
  // nodes)
  radio.enableDynamicPayloads();  // ACK payloads are dynamically sized

  // Acknowledgement packets have no payloads by default. We need to enable
  // this feature for all nodes (TX & RX) to use ACK payloads.
  radio.enableAckPayload();

  // set the RX address of the TX node into a RX pipe
  radio.openReadingPipe(0, ESMART3_MODULE);

  // setup the ACK payload & load the first response into the FIFO
  payload.counter = 0;
  payload.length = 0;
  payload.err_timeout_read_command = 0;
  payload.err_timeout_write_payload = 0;

  radio.writeAckPayload(0, &payload, PAYLOAD_HEAD_LEN + payload.length);

  radio.startListening();  // put radio in RX mode
}

uint32_t last_msg = 0;
uint8_t sent_byte = 0;
uint8_t bytes_to_send = 0;
uint32_t last_byte_received = 0;
bool modbus_in_complete = false;
uint8_t modbus_in[128];
uint8_t modbus_in_length = 0;
uint8_t modbus_out_offset = 0;

void loop() {
  uint8_t pipe;
  uint32_t start_timer = micros();
  if (radio.available(&pipe)) {
    uint8_t bytes =
        radio.getDynamicPayloadSize();  // get the size of the payload
    if (bytes <= sizeof(received)) {
      last_msg = start_timer;
      radio.read(&received, bytes);  // get incoming payload
      uint32_t end_timer = micros();
      if ((uint32_t)(end_timer - start_timer) > 150) {
        payload.err_timeout_read_command++;
      }

#ifdef DEBUG
      Serial.print(F("Received "));
      Serial.print(bytes);  // print the size of the payload
      Serial.print(F(" bytes on pipe "));
      Serial.print(pipe);  // print the pipe number
      Serial.print(F(": "));
      Serial.print(received.counter);  // print incoming counter
      Serial.print(F(" Sent: "));
      for (uint8_t i = 0; i < payload.length; i++) {
        Serial.print(payload.data[i]);
        Serial.print(' ');
      }
      Serial.println(payload.counter);  // print outgoing counter
      Serial.print(F("Time to receive send "));
      Serial.print(end_timer - start_timer);
      Serial.println(" us");
#endif
      // save incoming counter & increment for next outgoing
      payload.counter = received.counter + 1;

      payload.length = 0;
      payload.data_offset = modbus_out_offset;
      while (modbus_out_offset < modbus_in_length) {
	 payload.data[payload.length++] = modbus_in[modbus_out_offset++];
	 if (payload.length + PAYLOAD_HEAD_LEN == 32) {
	   break;
	 }
      }
      if (modbus_out_offset == modbus_in_length) {
	 payload.data_offset |= 0x80;
	 modbus_out_offset = 0;
	 modbus_in_length = 0;
	 modbus_in_complete = false;
      }

      start_timer = micros();
      radio.writeAckPayload(0, &payload, PAYLOAD_HEAD_LEN + payload.length);
      payload.length = 0;
      end_timer = micros();
      if ((uint32_t)(end_timer - start_timer) > 150) {
        payload.err_timeout_write_payload++;
      }
#ifdef DEBUG
      Serial.print(F("Time to update payload "));
      Serial.print(end_timer - start_timer);
      Serial.println(F(" us"));
#endif
      if (modbus_in_length == 0) {
	// send payload if any and no more bytes to send
	sent_byte = 0;
	bytes_to_send = received.length;
      }
    } else {
#ifdef DEBUG
      Serial.print(F("Invalid received message size: "));
      Serial.println(bytes);
#endif
    }
  }

  start_timer = micros();
  if (sent_byte < bytes_to_send) {
    sent_byte += Esmart3Serial.write(received.data[sent_byte]);
    modbus_in_complete = false;
  } else if (!modbus_in_complete) {
    int next = Esmart3Serial.read();
    if ((next >= 0) && (modbus_in_length < 128)) {
      last_byte_received = start_timer;
      modbus_in[modbus_in_length++] = next;
    } else if (modbus_in_length > 0) {
      uint32_t delta = start_timer - last_byte_received;
      if (delta > 3000) {  // end message if 3ms without byte
	modbus_in_complete = true;
	Serial.print(F("Bytes="));
	Serial.print(modbus_in_length);
	Serial.print(':');
	for (uint8_t i = 0;i < modbus_in_length;i++) {
	  Serial.print(' ');
	  Serial.print(modbus_in[i], HEX);
	}
	Serial.println();
      }
    }
  }

  if ((uint32_t)(start_timer - last_msg) > 2000000) {
  }
}
