#include <nRF24-SOLDERED.h>
#include "nrf_modules.h"
#include "private_haus.h"

// instantiate an object for the nRF24L01 transceiver
nRF24 radio(10, 9);  // using pin 7 for the CE pin, and pin 8 for the CSN pin

// uniquely identify which address this radio will use to transmit
bool radioNumber = 0;

RelayCommand relayCommand;

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
  Serial.println(F("RF24/examples/AcknowledgementPayloads"));

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
  radio.openWritingPipe(RELAY_MODULE);  // always uses pipe 0

  // set the RX address of the TX node into a RX pipe
  // radio.openReadingPipe(1, address[1]); // using pipe 1

  // setup the TX payload
  relayCommand.relay_old = 0;
  relayCommand.relay_new = 0;
  relayCommand.counter = 0;
  radio.stopListening();  // put radio in TX mode
}

void loop() {
  unsigned long start_timer = micros();  // start the timer
  bool report = radio.write(
      &relayCommand, sizeof(relayCommand));  // transmit & save the report
  unsigned long end_timer = micros();        // end the timer

  if (report) {
    Serial.print(F("Time to transmit = "));
    Serial.print(end_timer - start_timer);  // print the timer result
    Serial.print(F(" us. Sent"));
    uint8_t pipe;
    if (radio.available(&pipe)) {
      RelayPayload received;
      radio.read(&received, sizeof(received));  // get incoming ACK payload
      Serial.print(F(" Received "));
      Serial.print(
          radio.getDynamicPayloadSize());  // print incoming payload size
      Serial.print(F(" bytes on pipe "));
      Serial.print(pipe);  // print pipe number that received the ACK
      Serial.print(F(": "));
      Serial.print(received.raw[0]);
      Serial.print(' ');
      Serial.print(received.raw[1]);
      Serial.print(' ');
      Serial.print(received.raw[2]);
      Serial.print(' ');
      Serial.print(received.raw[3]);
      Serial.print(F(", emergency="));
      Serial.print(received.emergency_shutdown_reason);
      Serial.print(F(", errors="));
      Serial.print(received.err_timeout_read_command);
      Serial.print(' ');
      Serial.print(received.err_timeout_write_payload);
      Serial.print(' ');
      Serial.print(received.err_timeout_start_adc);
      Serial.print(' ');
      Serial.print(received.err_timeout_read_adc);
      Serial.print(F(", "));
      Serial.println(received.counter);  // print incoming counter

      // save incoming counter & increment for next outgoing
      relayCommand.counter++;
      relayCommand.relay_new++;
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
