#include <DS18B20.h>
#include <RF24.h>
#include <SPI.h>
#include <base64.h>
#include <driver/twai.h>
#include <esp_task_wdt.h>
#include <sml/sml_crc16.h>
#include <sml/sml_file.h>

#include "can_ids.h"
#include "defines.h"
#include "helper.h"
#include "hmDefines.h"
#include "hmInverter.h"
#include "hmPayload.h"
#include "hmSystem.h"
#include "template.h"
#include "../../../haus/private.h"

#define CAN_RX_PIN 26 /* G26 */
#define CAN_TX_PIN 27 /* G27 */

#define NRF24_CE 4     /* G4  - blue */
#define NRF24_IRQ 16   /* G16 - green */
#define NRF24_CSN 5    /* G5  - purple */
#define DS18B20_PIN 17 /* G17 - red copper wire */
#define NRF24_SCK 18   /* G18 - brown */
#define NRF24_MISO 19  /* G19 - orange */
#define RELAY_5V_1 21  /* G21 - gray */
#define RELAY_5V_2 22  /* G22 - white */
#define NRF24_MOSI 23  /* G23 - yellow */

#define LED_PIN 2
#define BUTTON_PIN 0 /* BOOT */

using namespace std;

SPIClass vspi(VSPI);
RF24 radio(NRF24_CE, NRF24_CSN);
Esmart3Command esmart3Command;
RelayCommand relayCommand;

DS18B20 ds(DS18B20_PIN);

uint32_t sml_lost_bytes_cnt = 0;
uint32_t sml_empty_message_cnt = 0;
uint32_t sml_error_cnt = 0;
uint32_t file_log_error_cnt = 0;

float consumption_Wh = 0.0;
float production_Wh = 0.0;
struct sml_buffer_s {
  time_t receive_time;
  int16_t valid_bytes;
  volatile bool locked;
  uint8_t data[258];  // including length bytes
} sml_buffers[3] = {
    {.valid_bytes = -1, .locked = false},
    {.valid_bytes = -1, .locked = false},
    {.valid_bytes = -1, .locked = false},
};

// First two fields are 1-0.96.50.1 and 1-0.96.1.0
// Then 1-0:1.8.0/255  consumption in Wh (float)
// Then 1-0:2.8.0/255  production in Wh (float)
// Then 1-0:16.7.0/255 Consumption actual in W (int)
// all enclosed in OPEN/CLOSE RESPONSE
//
// 1-0:96.1.0*255(001LOG0065800041)    Hersteller unabhängige
// Identifikationsnummer – Produktionsnummer 1-0:1.8.0*255(000000.0000*kWh)
// Kumulatives Register der aktiven Energie in kWh T1+T2
// 1-0:1.8.1*255(000000.0000*kWh)      Kumulatives Register der aktiven Energie
// in kWh T1 1-0:1.8.2*255(000000.0000*kWh)      Kumulatives Register der
// aktiven Energie in kWh T2 1-0:2.8.0*255(000000.0000*kWh)      -A Enerige
// 1-0:16.7.0*255(000000*W)            Stromeffektivwert
// 1-0:32.7.0*255(000.0*V)             Spannung L1, Auflösung 0.1 V
// 1-0:52.7.0*255(000.0*V)             Spannung L2, Auflösung 0.1 V
// 1-0:72.7.0*255(228.8*V)             Spannung L3, Auflösung 0.1 V
// 1-0:31.7.0*255(000.00*A)            Strom L1, Auflösung 0.01 A
// 1-0:51.7.0*255(000.00*A)            Strom L2, Auflösung 0.01 A
// 1-0:71.7.0*255(000.00*A)            Strom L3, Auf lösung 0.01 A
// 1-0:81.7.1*255(000*deg)             Phasenwinkel UL2 : UL1
// 1-0:81.7.2*255(000*deg)             Phasenwinkel UL3 : UL1
// 1-0:81.7.4*255(000*deg)             Phasenwinkel IL1 : UL1
// 1-0:81.7.15*255(000*deg)            Phasenwinkel IL2 : UL2
// 1-0:81.7.26*255(000*deg)            Phasenwinkel IL3 : UL3
// 1-0:14.7.0*255(50.0*Hz)             Netz Frequenz in Hz
// 1-0:1.8.0*96(00000.0*kWh)           Historischer Energieverbrauchswert vom
// letzten Tag (1d) 1-0:1.8.0*97(00000.0*kWh)           Historischer
// Energieverbrauchswert der letzten Woche (7d) 1-0:1.8.0*98(00000.0*kWh)
// Historischer Energieverbrauchswert des letzten Monats (30d)
// 1-0:1.8.0*99(00000.0*kWh)           Historischer Energieverbrauchswert des
// letzten Jahres (365d) 1-0:1.8.0*100(00000.0*kWh)          Historischer
// Energieverbrauchswert seit letzter Rückstellung
// 1-0:0.2.0*255(ver.03,432F,20170504) Firmware Version, Firmware Prüfsumme CRC
// , Datum 1-0:96.90.2*255(F0F6)               Prüfsumme - CRC der eingestellten
// Parameter 1-0:97.97.0*255(00000000)           FF - Status Register - Interner
// Gerätefehler
void publish(DynamicJsonDocument *json, sml_file *file) {
  (*json)["in_publish"] = true;
  for (int i = 0; i < file->messages_len; i++) {
    char name[20];
    sprintf(name, "Name %d", i);
    sml_message *message = file->messages[i];
    if (*message->message_body->tag == SML_MESSAGE_OPEN_RESPONSE) {
    } else if (*message->message_body->tag == SML_MESSAGE_CLOSE_RESPONSE) {
    } else if (*message->message_body->tag == SML_MESSAGE_GET_LIST_RESPONSE) {
      sml_list *entry;
      sml_get_list_response *body;
      body = (sml_get_list_response *)message->message_body->data;
      uint8_t index;
      for (entry = body->val_list, index = 0; entry != NULL;
           entry = entry->next, index++) {
        sprintf(name, "Name %d-%d", i, index);
        if (!entry->value) {  // do not crash on null value
          continue;
        }

        char obisIdentifier[32];
        char buffer[255];

        sprintf(obisIdentifier, "%d-%d:%d.%d.%d/%d", entry->obj_name->str[0],
                entry->obj_name->str[1], entry->obj_name->str[2],
                entry->obj_name->str[3], entry->obj_name->str[4],
                entry->obj_name->str[5]);

        if (((entry->value->type & SML_TYPE_FIELD) == SML_TYPE_INTEGER) ||
            ((entry->value->type & SML_TYPE_FIELD) == SML_TYPE_UNSIGNED)) {
          double value = sml_value_to_double(entry->value);
          int scaler = (entry->scaler) ? *entry->scaler : 0;
          int prec = -scaler;
          if (prec < 0) prec = 0;
          value = value * pow(10, scaler);
          sprintf(buffer, "%s %.*f", obisIdentifier, prec, value);
          (*json)[name] = buffer;
          if (strcmp(obisIdentifier, "1-0:1.8.0/255") == 0) {
            (*json)["Consumption_Wh"] = value;
            consumption_Wh = value;
          } else if (strcmp(obisIdentifier, "1-0:2.8.0/255") == 0) {
            (*json)["Production_Wh"] = value;
            production_Wh = value;
          } else if (strcmp(obisIdentifier, "1-0:16.7.0/255") == 0) {
            if (entry->status &&
                ((*entry->status->data.status16 & 0x20) != 0)) {
              value = -value;
            }
            (*json)["Power_W"] = value;
          }
        } else if (entry->value->type == SML_TYPE_OCTET_STRING) {
          char *value;
          sml_value_to_strhex(entry->value, &value, true);
          (*json)[name] = value;
          free(value);
        } else if (entry->value->type == SML_TYPE_BOOLEAN) {
          (*json)[name] = entry->value->data.boolean;
        }

        else {
          sprintf(buffer, "Unknown %s %d", obisIdentifier,
                  entry->value->type & SML_TYPE_FIELD);
          (*json)[name] = buffer;
        }
      }
    } else {
      sprintf(name, "Unknown_tag %d", i);
      (*json)[name] = *message->message_body->tag;
    }
  }
}

const uint8_t sml_header[8] = {0x1b, 0x1b, 0x1b, 0x1b, 0x01, 0x01, 0x01, 0x01};

void json_publish(DynamicJsonDocument *json) {
  (*json)["publish"] = "called";
  for (uint8_t i = 0; i < 3; i++) {
    struct sml_buffer_s *buf = &sml_buffers[i];
    if (buf->locked || buf->valid_bytes < 0) {
      continue;
    }
    buf->locked = true;
    // read back, if it was really locked
    if (buf->locked) {
      if (buf->valid_bytes >= 0) {
        (*json)["can_b64"] = base64::encode(&buf->data[2], buf->valid_bytes);
        bool err = true;
        if (buf->valid_bytes > 16) {
          if (memcmp(&buf->data[2], sml_header, 8) == 0) {
            uint16_t chksum =
                sml_crc16_calculate(&buf->data[2], buf->valid_bytes - 2);
            if (((chksum >> 8) == buf->data[buf->valid_bytes]) &&
                ((chksum & 0xff) == buf->data[buf->valid_bytes + 1])) {
              sml_file *file =
                  sml_file_parse(&buf->data[2 + 8], buf->valid_bytes - 16);
              (*json)["valid_sml"] = file->messages_len;
              (*json)["sml_time"] = buf->receive_time;
              publish(json, file);
              sml_file_free(file);
              err = false;
            }
          }
        }
        if (err) {
          (*json)["sml_error"] = true;
          sml_error_cnt++;
        }
        buf->locked = false;
        break;
      }
      buf->locked = false;
    }
  }
}

void json_update(DynamicJsonDocument *json) {
  //  if (json->containsKey("moveBoth")) {
  //    int32_t steps = (*json)["moveBoth"];
  //    stepper1->move(steps);
  //    stepper2->move(steps);
  //  }
}

//---------------------------------------------------
#define STACK_SIZE 2000
#define PRIORITY configMAX_PRIORITIES
#define POLLING_RATE_MS 1000

uint16_t sml_base_id = 0;
uint16_t sml_received_length = 0;
int8_t receive_buffer = 0;
uint32_t can_tx_error_cnt = 0;
uint32_t can_tx_retry_cnt = 0;
uint32_t can_rx_error_cnt = 0;
uint32_t can_rx_queue_full_cnt = 0;
uint32_t can_error_passive_cnt = 0;
uint32_t can_error_bus_error_cnt = 0;
uint32_t can_receive_cnt = 0;

static void handle_rx_message(twai_message_t &message) {
  // Process received message
  Serial.print(message.extd ? "Extended" : "Standard");
  Serial.printf("-ID: %x Bytes:", message.identifier);
  if (!(message.rtr)) {
    for (int i = 0; i < message.data_length_code; i++) {
      Serial.printf(" %d = %02x,", i, message.data[i]);
    }
    Serial.println("");

    if ((message.identifier & ~0x1ff) == CAN_ID_STROMZAEHLER_INFO_BASIS) {
      struct sml_buffer_s *active = &sml_buffers[receive_buffer];
      int8_t buf_i = receive_buffer;
      uint16_t this_id = message.identifier & ~0xff;
      if (this_id != sml_base_id) {
        if (sml_received_length > 2) {
          Serial.println("Throw away received data");
          sml_lost_bytes_cnt++;
        }
        // new sml message
        // find buffer
        for (uint8_t i = 0; i < 3; i++) {
          if ((i != receive_buffer) && !sml_buffers[i].locked) {
            receive_buffer = i;
            active = &sml_buffers[i];
            break;
          }
        }
        sml_received_length = 0;
        active->valid_bytes = -1;
        active->locked = false;
        active->data[0] = 0;
        active->data[1] = 0;
        sml_base_id = this_id;
      }
      if (message.data_length_code == 0) {
        sml_empty_message_cnt++;
      }
      uint16_t off = message.identifier & 0xff;
      off <<= 3;
      for (uint8_t i = 0; i < message.data_length_code; i++) {
        active->data[off++] = message.data[i];
      }
      sml_received_length += message.data_length_code;
      uint16_t expected_length = active->data[1];
      expected_length <<= 8;
      expected_length += active->data[0];
      if (expected_length + 2 == sml_received_length) {
        Serial.println("Received OK data");
        active->valid_bytes = sml_received_length - 2;
        time_t now = time(nullptr);
        active->receive_time = now;
        sml_received_length = 0;
        sml_base_id = 0;
      }
    }
  }
}

void CANTask(void *parameter) {
  while (true) {
    // Check if alert happened
    uint32_t alerts_triggered;
    twai_read_alerts(&alerts_triggered, pdMS_TO_TICKS(POLLING_RATE_MS));
    twai_status_info_t twaistatus;
    twai_get_status_info(&twaistatus);

    // Handle alerts
    if (alerts_triggered & TWAI_ALERT_ERR_PASS) {
      Serial.println("Alert: TWAI controller has become error passive.");
      can_error_passive_cnt++;
    }
    if (alerts_triggered & TWAI_ALERT_BUS_ERROR) {
      Serial.println(
          "Alert: A (Bit, Stuff, CRC, Form, ACK) error has occurred on the "
          "bus.");
      Serial.printf("Bus error count: %d\n", twaistatus.bus_error_count);
      can_error_bus_error_cnt++;
    }
    if (alerts_triggered & TWAI_ALERT_RX_QUEUE_FULL) {
      Serial.println(
          "Alert: The RX queue is full causing a received frame to be lost.");
      Serial.printf("RX buffered: %d\t", twaistatus.msgs_to_rx);
      Serial.printf("RX missed: %d\t", twaistatus.rx_missed_count);
      Serial.printf("RX overrun %d\n", twaistatus.rx_overrun_count);
      can_rx_queue_full_cnt++;
    }
    if (alerts_triggered & TWAI_ALERT_TX_FAILED) {
      can_tx_error_cnt++;
    }
    if (alerts_triggered & TWAI_ALERT_TX_RETRIED) {
      can_tx_retry_cnt++;
    }

    // Check if message is received
    if (alerts_triggered & TWAI_ALERT_RX_DATA) {
      // One or more messages received. Handle all.
      can_receive_cnt++;
      twai_message_t message;
      while (twai_receive(&message, 0) == ESP_OK) {
        handle_rx_message(message);
      }
    }
    esp_task_wdt_reset();
  }
}
//---------------------------------------------------
void setup() {
  tpl_system_setup(0);  // no deep sleep

  digitalWrite(RELAY_5V_1, HIGH);
  digitalWrite(RELAY_5V_2, HIGH);
  pinMode(RELAY_5V_1, OUTPUT);
  pinMode(RELAY_5V_2, OUTPUT);

  Serial.begin(115200);
  Serial.setDebugOutput(false);

  // Wait OTA
  //  tpl_wifi_setup(true, true, (gpio_num_t)tpl_ledPin);
  tpl_wifi_setup(true, true, (gpio_num_t)LED_PIN);
  tpl_webserver_setup();
  tpl_websocket_setup(json_publish, json_update);
  tpl_net_watchdog_setup();
  tpl_command_setup(NULL);

#ifdef IS_ESP32CAM
  uint8_t fail_cnt = 0;
#ifdef BOTtoken
  tpl_camera_setup(&fail_cnt, FRAMESIZE_QVGA);
#else
  tpl_camera_setup(&fail_cnt, FRAMESIZE_VGA);
#endif
#endif

#ifdef BOTtoken
  tpl_telegram_setup(BOTtoken, CHAT_ID);
#endif

  tpl_server.on("/can", HTTP_GET, []() {
    tpl_server.sendHeader("Connection", "close");
    char can_info[255];
    sprintf(can_info,
            "Error: %d\n"
            "SML: %d empty, %d incomplete, %d errors\nTX: %d errors, %d "
            "retries\nRX: %d messages, %d errors, %d queue_full\nERROR: %d "
            "passive cnt, % bus error\n",
            file_log_error_cnt, sml_empty_message_cnt, sml_lost_bytes_cnt,
            sml_error_cnt, can_tx_error_cnt, can_tx_retry_cnt, can_receive_cnt,
            can_rx_error_cnt, can_rx_queue_full_cnt, can_error_passive_cnt,
            can_error_bus_error_cnt);
    tpl_server.send(200, "text/html", can_info);
  });

  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
      (gpio_num_t)CAN_TX_PIN, (gpio_num_t)CAN_RX_PIN, TWAI_MODE_NORMAL);
  twai_timing_config_t t_config =
      TWAI_TIMING_CONFIG_125KBITS();  // Look in the api-reference for other
                                      // speed sets.
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  // Install TWAI driver
  if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
    Serial.println("TWAI Driver installed");
  } else {
    Serial.println("Failed to install TWAI driver");
    return;
  }
  // Start TWAI driver
  if (twai_start() == ESP_OK) {
    Serial.println("TWAI Driver started");
  } else {
    Serial.println("Failed to start TWAI driver");
    return;
  }

  // Reconfigure alerts to detect frame receive, Bus-Off error and RX queue
  // full states
  uint32_t alerts_to_enable = TWAI_ALERT_RX_DATA | TWAI_ALERT_ERR_PASS |
                              TWAI_ALERT_BUS_ERROR | TWAI_ALERT_RX_QUEUE_FULL |
                              TWAI_ALERT_TX_FAILED | TWAI_ALERT_TX_RETRIED;
  if (twai_reconfigure_alerts(alerts_to_enable, NULL) == ESP_OK) {
    Serial.println("CAN Alerts reconfigured");
    xTaskCreate(CANTask, "CANTask", STACK_SIZE, NULL, PRIORITY, NULL);
  } else {
    Serial.println("Failed to reconfigure alerts");
    return;
  }

  // Set the PA Level low to try preventing power supply related problems
  // because these examples are likely run with nodes in close proximity to
  // each other.
  //  SPI.begin(NRF24_SCK, NRF24_MISO, NRF24_MOSI, ELINK_SS);
  vspi.begin();
  radio.begin(&vspi);
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
  radio.openWritingPipe(ESMART3_MODULE);

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

  relayCommand.relay_old = 0;
  relayCommand.relay_new = 0;
  relayCommand.counter = 0;

  radio.stopListening();  // put radio in TX mode

  Serial.println("Setup done.");
}

uint32_t next_stack_info = 0;

uint8_t last_sec = 255;

void communicate_esmart3() {
  unsigned long start_timer = micros();  // start the timer
  radio.openWritingPipe(ESMART3_MODULE);
  radio.stopListening();
  bool report = radio.write(
      &esmart3Command,
      COMMAND_HEAD_LEN + esmart3Command.length);  // transmit & save the report
  unsigned long end_timer = micros();             // end the timer

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
      if (bytes != PAYLOAD_HEAD_LEN + received.length) {
        Serial.println(F("..INVALID MESSAGE"));
      } else {
        Serial.print(F(": ["));
        for (uint8_t i = 0; i < received.length; i++) {
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
}

void communicate_relay_module() {
  unsigned long start_timer = micros();  // start the timer
  radio.openWritingPipe(RELAY_MODULE);
  radio.stopListening();
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
}

void loop() {
  uint32_t ms = millis();
  if ((int32_t)(ms - next_stack_info) > 0) {
    next_stack_info = ms + 500;
    tpl_update_stack_info();
    Serial.println(tpl_config.stack_info);

    while (ds.selectNext()) {
      ds.setResolution(12);
      Serial.print("Temperature=");
      float temp = ds.getTempC();
      Serial.print(temp);
      Serial.print(" => ");
      Serial.println(temp - 3.2);  // sensor specific calibration
    }
    communicate_esmart3();
    communicate_relay_module();
    //    digitalWrite(RELAY_5V_1, digitalRead(RELAY_5V_1) == HIGH ? LOW:HIGH);
  }

  //  struct tm timeinfo;
  //  time_t now = time(nullptr);
  //  localtime_r(&now, &timeinfo);
  //  if (timeinfo.tm_sec != last_sec) {
  //    last_sec = timeinfo.tm_sec;
  //    twai_message_t message;
  //    message.flags = TWAI_MSG_FLAG_NONE;
  //    message.self = 1;
  //    message.identifier = CAN_ID_TIMESTAMP;
  //    message.data_length_code = 4;
  //    message.data[0] = timeinfo.tm_sec;
  //    message.data[1] = timeinfo.tm_min;
  //    message.data[2] = timeinfo.tm_hour;
  //    message.data[3] = timeinfo.tm_wday;
  //    twai_transmit(&message, portMAX_DELAY);
  //
  //    if (timeinfo.tm_sec == 0) {
  //      Serial.println("update display");
  //    }
  //  }
  const TickType_t xDelay = 100 / portTICK_PERIOD_MS;
  vTaskDelay(xDelay);
}
