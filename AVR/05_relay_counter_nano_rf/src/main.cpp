#include <nRF24-SOLDERED.h>
#include <ADS1115_WE.h>
#include <Wire.h>

#include "nrf_modules.h"
#include "private_haus.h"

// #define DEBUG

// V1.0 !!!!
// https://github.com/emakefun/rf-nano/blob/master/schematic/rf-nano_sch_v3.0.pdf
#define RELAY_SSR 2   /* D2 */
#define PIN_RF_CS 9   // D9 = Chip Select (CS) for nRF24L01+
#define PIN_RF_CE 10  // D10 = Chip Enable (CE) for nRF24L01+
// Pin 10 is PB2 - SS
// Pin 11 is PB3 - MOSI
// Pin 12 is PB4 - MISO
// Pin 13 is PB5 - SCK
#define RELAY_0 14 /* A0 */
#define RELAY_1 15 /* A1 */
#define RELAY_2 16 /* A2 */
#define RELAY_3 17 /* A3 */

#define I2C_SDA 18 /* A4 */
#define I2C_SCK 19 /* A5 */

// instantiate an object for the nRF24L01 transceiver
nRF24 radio(PIN_RF_CE, PIN_RF_CS);

ADS1115_WE adc = ADS1115_WE();  // uses Wire / I2C Address = 0x48
uint8_t adc_channel = 0;
bool start_adc = true;
int32_t raw_avg[4] = {0, 0, 0, 0};
bool raw_avg_valid[4] = {false, false, false, false};

const uint8_t outputs[5] = {RELAY_0, RELAY_1, RELAY_2, RELAY_3, RELAY_SSR};
const struct adc_settings_s {
  ADS1115_MUX mux;
  ADS1115_RANGE range;
} adc_setting[4] = {
    {ADS1115_COMP_0_GND, ADS1115_RANGE_4096},
    {ADS1115_COMP_1_GND, ADS1115_RANGE_4096},
    {ADS1115_COMP_2_GND, ADS1115_RANGE_4096},
    {ADS1115_COMP_3_GND, ADS1115_RANGE_6144},
};

RelayPayload payload;

void setup() {
  for (uint8_t i = 0; i < 5; i++) {
    uint8_t pin = outputs[i];
    digitalWrite(pin, LOW);
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }

  Wire.begin();
  Wire.setClock(500000);

  Serial.begin(115200);

  while (!adc.init()) {
    Serial.println("ADS1115 not connected!");
    delay(1000);
    // hold in infinite loop
  }

  /* Set the conversion rate in SPS (samples per second)
   * Options should be self-explaining:
   *
   *  ADS1115_8_SPS
   *  ADS1115_16_SPS
   *  ADS1115_32_SPS
   *  ADS1115_64_SPS
   *  ADS1115_128_SPS (default)
   *  ADS1115_250_SPS
   *  ADS1115_475_SPS
   *  ADS1115_860_SPS
   */
  adc.setConvRate(ADS1115_860_SPS);

  /* Set continuous or single shot mode:
   *
   *  ADS1115_CONTINUOUS  ->  continuous mode
   *  ADS1115_SINGLE     ->  single shot mode (default)
   */
  adc.setMeasureMode(
      ADS1115_SINGLE);  // comment line/change parameter to change mode

  // initialize the transceiver on the SPI bus
  while (!radio.begin()) {
    Serial.println(F("radio hardware is not responding!!"));
    delay(1000);
    // hold in infinite loop
  }

  Serial.println(F("Relay module"));

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
  // radio.openWritingPipe(address[1]); // always uses pipe 0

  // set the RX address of the TX node into a RX pipe
  radio.openReadingPipe(0, RELAY_MODULE);

  // setup the ACK payload & load the first response into the FIFO
  payload.counter = 0;
  payload.raw[0] = 0;
  payload.raw[1] = 0;
  payload.raw[2] = 0;
  payload.raw[3] = 0;
  payload.conversions = 0;
  payload.emergency_shutdown_reason = 0;
  payload.err_timeout_read_command = 0;
  payload.err_timeout_write_payload = 0;
  payload.err_timeout_start_adc = 0;
  payload.err_timeout_read_adc = 0;

  radio.writeAckPayload(0, &payload, sizeof(payload));

  radio.startListening();  // put radio in RX mode

  // For debugging info
  // printf_begin();             // needed only once for printing details
  // radio.printDetails();       // (smaller) function that prints raw register
  // values radio.printPrettyDetails(); // (larger) function that prints human
  // readable data
}

uint32_t last_msg = 0;
uint8_t relay_state_old = 0;
uint8_t relay_state_new = 0;

void loop() {
  uint8_t pipe;
  uint32_t start_timer = micros();
  if (radio.available(&pipe)) {
    uint8_t bytes =
        radio.getDynamicPayloadSize();  // get the size of the payload
    RelayCommand received;
    if (bytes == sizeof(received)) {
      last_msg = start_timer;
      radio.read(&received, sizeof(received));  // get incoming payload
      uint32_t end_timer = micros();
      if ((uint32_t)(end_timer - start_timer) > 150) {
        payload.err_timeout_read_command++;
      }

      relay_state_new = received.relay_new;

#ifdef DEBUG
      Serial.print(F("Received "));
      Serial.print(bytes);  // print the size of the payload
      Serial.print(F(" bytes on pipe "));
      Serial.print(pipe);  // print the pipe number
      Serial.print(F(": "));
      Serial.print(received.counter);  // print incoming counter
      Serial.print(F(": "));
      Serial.print(received.relay_new);
      Serial.print(F(" Sent: "));
      for (uint8_t i = 0; i < 4; i++) {
        Serial.print(payload.raw[i]);
        Serial.print(' ');
      }
      Serial.println(payload.counter);  // print outgoing counter
      Serial.print(F("Time to receive send "));
      Serial.print(end_timer - start_timer);
      Serial.println(" us");
#endif

      // save incoming counter & increment for next outgoing
      payload.counter = received.counter + 1;
      // load the payload for the first received transmission on pipe 0
      start_timer = micros();
      radio.writeAckPayload(0, &payload, sizeof(payload));
      end_timer = micros();
      if ((uint32_t)(end_timer - start_timer) > 150) {
        payload.err_timeout_write_payload++;
      }
#ifdef DEBUG
      Serial.print(F("Time to update payload "));
      Serial.print(end_timer - start_timer);
      Serial.println(F(" us"));
#endif
    } else {
#ifdef DEBUG
      Serial.print(F("Invalid received message size: "));
      Serial.println(bytes);
#endif
    }
  }

  if ((uint32_t)(start_timer - last_msg) > 2000000) {
    payload.emergency_shutdown_reason |= EMERGENCY_NRF24_TIMEOUT;
  }

  if (payload.emergency_shutdown_reason != 0) {
    // TODO: set up state machine for shutdown sequence
    relay_state_new = 0;
  }

  if ((relay_state_new != relay_state_old) ||
      (payload.emergency_shutdown_reason != 0)) {
    digitalWrite(RELAY_0, relay_state_new & 1 ? HIGH : LOW);
    digitalWrite(RELAY_1, relay_state_new & 2 ? HIGH : LOW);
    digitalWrite(RELAY_2, relay_state_new & 4 ? HIGH : LOW);
    digitalWrite(RELAY_3, relay_state_new & 8 ? HIGH : LOW);
    digitalWrite(RELAY_SSR, relay_state_new & 16 ? HIGH : LOW);
    relay_state_old = relay_state_new;
  }

  start_timer = micros();
  // start adc: ~3440 us
  if (start_adc) {
    start_adc = false;
    adc.setCompareChannels(adc_setting[adc_channel].mux);
    adc.setVoltageRange_mV(adc_setting[adc_channel].range);
    adc.startSingleMeasurement();
    uint32_t end_timer = micros();
    if ((uint32_t)(end_timer - start_timer) > 4000) {
      payload.err_timeout_start_adc++;
    }
#ifdef DEBUG
    Serial.print(F("Time to start measurement "));
    Serial.print(end_timer - start_timer);
    Serial.println(F(" us"));
#endif
  } else if (!adc.isBusy()) {
    // read adc: ~340-380 us
#define AVG 8
    int32_t raw = adc.getRawResult();
    payload.conversions++;
    raw *= AVG;
    if (raw_avg_valid[adc_channel]) {
      int32_t delta = raw - raw_avg[adc_channel];
      raw_avg[adc_channel] += delta / AVG;
    } else {
      raw_avg_valid[adc_channel] = true;
      raw_avg[adc_channel] = raw;
    }

    payload.raw[adc_channel] = (raw_avg[adc_channel] + AVG / 2) / AVG;

    adc_channel = (adc_channel + 1) & 3;
    start_adc = true;

    uint32_t end_timer = micros();
    if ((uint32_t)(end_timer - start_timer) > 500) {
      payload.err_timeout_read_adc++;
    }

    if (payload.err_timeout_read_command > 3000) {
      payload.emergency_shutdown_reason |= EMERGENCY_READ_COMMAND_TIMEOUTS;
    }
    if (payload.err_timeout_write_payload > 1000) {
      payload.emergency_shutdown_reason |= EMERGENCY_WRITE_PAYLOAD_TIMEOUTS;
    }
    if (payload.err_timeout_start_adc > 1000) {
      payload.emergency_shutdown_reason |= EMERGENCY_START_ADC_TIMEOUTS;
    }
    if (payload.err_timeout_read_adc > 1000) {
      payload.emergency_shutdown_reason |= EMERGENCY_READ_ADC_TIMEOUTS;
    }

#ifdef DEBUG
    Serial.print(raw);
    Serial.print(' ');
    Serial.print(raw_avg[(adc_channel + 3) & 3]);
    Serial.print(' ');
    Serial.print(F("Time to read measurement "));
    Serial.print(end_timer - start_timer);
    Serial.println(F(" us"));
#endif
  }
}
