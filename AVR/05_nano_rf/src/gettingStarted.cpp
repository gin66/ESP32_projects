#include <nRF24-SOLDERED.h>
#include <DS18B20.h>
#include <mcp2515.h>

// instantiate an object for the nRF24L01 transceiver
nRF24 radio(10, 9);  // using pin 10 for the CE pin, and pin 9 for the CSN pin

DS18B20 ds(2);

struct can_frame canMsg;
MCP2515 mcp2515(8);  // using pin 8 for CAN CS

// Let these addresses be used for the pair
uint8_t address[][6] = {"1Node", "2Node"};
// It is very helpful to think of an address as a path instead of as
// an identifying device destination

// to use different addresses on a pair of radios, we need a variable to
// uniquely identify which address this radio will use to transmit
bool radioNumber =
    1;  // 0 uses address[0] to transmit, 1 uses address[1] to transmit

// Used to control whether this node is sending or receiving
bool role = false;  // true = TX role, false = RX role

// For this example, we'll be using a payload containing
// a single float number that will be incremented
// on every successful transmission
float payload = 0.0;

void setup() {
  Serial.begin(115200);

  MCP2515::ERROR res = mcp2515.reset();
  if (res == MCP2515::ERROR_OK) {
    mcp2515.setBitrate(CAN_125KBPS);
    mcp2515.setLoopbackMode();
  } else {
    Serial.print("Error initializing mcp2515");
  }

  Serial.print("Devices: ");
  Serial.println(ds.getNumberOfDevices());
  Serial.println();

  // should be obsolete
  pinMode(9, OUTPUT);
  pinMode(10, OUTPUT);

  // initialize the transceiver on the SPI bus
  if (!radio.begin()) {
    Serial.println(F("radio hardware is not responding!!"));
    while (1) {
    }  // hold in infinite loop
  }
  radio.setDataRate(RF24_250KBPS);
  radio.setCRCLength(RF24_CRC_16);

  // print example's introductory prompt
  Serial.println(F("RF24/examples/GettingStarted"));

  // To set the radioNumber via the Serial monitor on startup
  Serial.println(F("Which radio is this? Enter '0' or '1'. Defaults to '0'"));
  char input = 1;
  while (true) {
    // wait for user input
    if (Serial.available()) {
      input = Serial.parseInt();
      break;
    }
    if (millis() > 5000) {
      break;
    }
  }
  radioNumber = input == 1;
  Serial.print(F("radioNumber = "));
  Serial.println((int)radioNumber);

  // role variable is hardcoded to RX behavior, inform the user of this
  Serial.println(F("*** PRESS 'T' to begin transmitting to the other node"));

  // Set the PA Level low to try preventing power supply related problems
  // because these examples are likely run with nodes in close proximity to
  // each other.
  // radio.setPALevel(RF24_PA_LOW); // RF24_PA_MAX is default.
  radio.setPALevel(RF24_PA_MAX);  // RF24_PA_MAX is default.

  // save on transmission time by setting the radio to only transmit the
  // number of bytes we need to transmit a float
  radio.setPayloadSize(sizeof(payload));  // float datatype occupies 4 bytes

  // set the TX address of the RX node into the TX pipe
  radio.openWritingPipe(address[radioNumber]);  // always uses pipe 0

  // set the RX address of the TX node into a RX pipe
  radio.openReadingPipe(1, address[!radioNumber]);  // using pipe 1

  // additional setup specific to the node's role
  if (role) {
    radio.stopListening();  // put radio in TX mode
  } else {
    radio.startListening();  // put radio in RX mode
  }

  // For debugging info
  // printf_begin();             // needed only once for printing details
  // radio.printDetails();       // (smaller) function that prints raw register
  // values radio.printPrettyDetails(); // (larger) function that prints human
  // readable data

}  // setup

void loop() {
  while (ds.selectNext()) {
    switch (ds.getFamilyCode()) {
      case MODEL_DS18S20:
        Serial.println("Model: DS18S20/DS1820");
        break;
      case MODEL_DS1822:
        Serial.println("Model: DS1822");
        break;
      case MODEL_DS18B20:
        Serial.println("Model: DS18B20");
        break;
      default:
        Serial.println("Unrecognized Device");
        break;
    }

    uint8_t address[8];
    ds.getAddress(address);

    Serial.print("Address:");
    for (uint8_t i = 0; i < 8; i++) {
      Serial.print(" ");
      Serial.print(address[i]);
    }
    Serial.println();

    Serial.print("Resolution: ");
    Serial.println(ds.getResolution());

    Serial.print("Power Mode: ");
    if (ds.getPowerMode()) {
      Serial.println("External");
    } else {
      Serial.println("Parasite");
    }

    Serial.print("Temperature: ");
    Serial.print(ds.getTempC());
    Serial.print(" C / ");
    Serial.print(ds.getTempF());
    Serial.println(" F");
    Serial.println();
  }

  if (role) {
    // This device is a TX node

    unsigned long start_timer = micros();  // start the timer
    bool report =
        radio.write(&payload, sizeof(float));  // transmit & save the report
    unsigned long end_timer = micros();        // end the timer

    if (report) {
      Serial.print(F("Transmission successful! "));  // payload was delivered
      Serial.print(F("Time to transmit = "));
      Serial.print(end_timer - start_timer);  // print the timer result
      Serial.print(F(" us. Sent: "));
      Serial.println(payload);  // print payload sent
      payload += 0.01;          // increment float payload
    } else {
      Serial.println(
          F("Transmission failed or timed out"));  // payload was not delivered
    }

    // to make this example readable in the serial monitor
    delay(1000);  // slow transmissions down by 1 second
  } else {
    // This device is a RX node

    uint8_t pipe;
    if (radio.available(&pipe)) {  // is there a payload? get the pipe number
                                   // that recieved it
      uint8_t bytes = radio.getPayloadSize();  // get the size of the payload
      radio.read(&payload, bytes);             // fetch payload from FIFO
      Serial.print(F("Received "));
      Serial.print(bytes);  // print the size of the payload
      Serial.print(F(" bytes on pipe "));
      Serial.print(pipe);  // print the pipe number
      Serial.print(F(": "));
      Serial.println(payload);  // print the payload's value
    }
  }  // role

  if (Serial.available()) {
    // change the role via the serial monitor

    char c = toupper(Serial.read());
    if (c == 'T' && !role) {
      // Become the TX node

      role = true;
      Serial.println(
          F("*** CHANGING TO TRANSMIT ROLE -- PRESS 'R' TO SWITCH BACK"));
      radio.stopListening();
    } else if (c == 'R' && role) {
      // Become the RX node

      role = false;
      Serial.println(
          F("*** CHANGING TO RECEIVE ROLE -- PRESS 'T' TO SWITCH BACK"));
      radio.startListening();
    }
  }

}  // loop
