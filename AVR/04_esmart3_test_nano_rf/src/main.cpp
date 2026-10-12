#include <nRF24-SOLDERED.h>
#include "nrf_modules.h"
#include "private_haus.h"

// instantiate an object for the nRF24L01 transceiver
nRF24 radio(10, 9);  // using pin 7 for the CE pin, and pin 8 for the CSN pin

// uniquely identify which address this radio will use to transmit
bool radioNumber = 0;

Esmart3Command esmart3Command;

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    // some boards need to wait to ensure access to serial over USB
  }

  // initialize the transceiver on the SPI bus
  if (!radio.begin()) {
    Serial.println(F("radio hardware is not responding!!"));
    while (1) {
    }  // hold in infinite loop
  }

  // print example's introductory prompt
  Serial.println(F("Esmart3 test"));

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

  // set the TX address of the RX node into the TX pipe
  radio.openWritingPipe(ESMART3_MODULE);  // always uses pipe 0

  // setup the TX payload
  esmart3Command.length = 10;
  esmart3Command.counter = 0;
  esmart3Command.data[0] = 0xaa;
  esmart3Command.data[1] = 0x01;
  esmart3Command.data[2] = 0x00;
  esmart3Command.data[3] = 0x01;
  esmart3Command.data[4] = 0x00;
  esmart3Command.data[5] = 0x03;
  esmart3Command.data[6] = 0x00;
  esmart3Command.data[7] = 0x00;
  esmart3Command.data[8] = 0x1a;
  esmart3Command.data[9] = 0x37;
  radio.stopListening();  // put radio in TX mode
}

void loop() {
  unsigned long start_timer = micros();  // start the timer
  bool report = radio.write(
      &esmart3Command, COMMAND_HEAD_LEN+esmart3Command.length);  // transmit & save the report
  unsigned long end_timer = micros();        // end the timer

  if (report) {
    Serial.print(F("sent:"));
    Serial.print(end_timer - start_timer);  // print the timer result
    Serial.print(F(" us."));
    uint8_t pipe;
    if (radio.available(&pipe)) {
      Esmart3Payload received;
      radio.read(&received, sizeof(received));  // get incoming ACK payload
      uint8_t bytes = radio.getDynamicPayloadSize();
      Serial.print(F(" Got "));
      Serial.print(bytes);
      Serial.print(F(" bytes"));
      if (bytes != PAYLOAD_HEAD_LEN+received.length) {
	 Serial.println(F("..INVALID MESSAGE"));
      }
      else {
	      Serial.print(F(": ["));
	      for (uint8_t i = 0;i < received.length;i++) {
		 Serial.print(received.data[i], HEX);
		 Serial.print(' ');
	      }
	      Serial.print(F("], errors="));
	      Serial.print(received.err_timeout_read_command);
	      Serial.print(' ');
	      Serial.print(received.err_timeout_write_payload);
	      Serial.print(F(", Cnt="));
	      Serial.println(received.counter);  // print incoming counter
      }
      // save incoming counter & increment for next outgoing
      esmart3Command.counter++;
    } else {
      Serial.println(
          F(" Received: an empty ACK packet"));  // empty ACK packet received
    }
  } else {
    Serial.println(
        F("Transmission failed or timed out"));  // payload was not delivered
  }

  // to make this example readable in the serial monitor
  delay(1000);  // slow transmissions down by 1 second
}  // loop
