#include <GxEPD.h>
#include <GxGDEH0213B73/GxGDEH0213B73.h>  // 2.13" b/w newer panel

#include "can_ids.h"
#include "template.h"
// FreeFonts from Adafruit_GFX
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <Fonts/FreeMonoBold24pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <GxIO/GxIO.h>
#include <GxIO/GxIO_SPI/GxIO_SPI.h>
#include <SD.h>
#include <SPI.h>
#include <base64.h>
#include <driver/twai.h>
#include <esp_task_wdt.h>
#include <sml/sml_crc16.h>
#include <sml/sml_file.h>

#define SPI_MOSI 23
#define SPI_MISO -1
#define SPI_CLK 18

#define ELINK_SS 5
#define ELINK_BUSY 4
#define ELINK_RESET 16
#define ELINK_DC 17

#define SDCARD_SS 13
#define SDCARD_CLK 14
#define SDCARD_MOSI 15
#define SDCARD_MISO 2

#define CAN_TX_PIN 21
#define CAN_RX_PIN 22

using namespace std;

#define LED_PIN 19
#define BUTTON_PIN 39 /* analog read 4095 unpressed, 0 pressed */

GxIO_Class io(SPI, /*CS=5*/ ELINK_SS, /*DC=*/ELINK_DC, /*RST=*/ELINK_RESET);
GxEPD_Class display(io, /*RST=*/ELINK_RESET, /*BUSY=*/ELINK_BUSY);

SPIClass sdSPI(VSPI);

bool sdOK = false;

uint32_t sml_lost_bytes_cnt = 0;
uint32_t sml_empty_message_cnt = 0;
uint32_t sml_error_cnt = 0;
uint32_t file_log_error_cnt = 0;

volatile float consumption_Wh = 0.0;
volatile float production_Wh = 0.0;
volatile float current_W = 0.0;
volatile uint32_t write_before = 0;
volatile uint32_t write_after = 0;

// --- Minute power history ring buffer (same pattern as 14_power_control) ---
#define HISTORY_MINUTES (8 * 60)        // 8 h, 8 bytes/entry = 3.84 KB
struct minute_entry_s { float w_min, w_max; };
static struct minute_entry_s hist_buf[HISTORY_MINUTES];
static uint32_t hist_write_idx = 0;    // next slot to write (monotonic)
static uint32_t hist_read_idx  = 0;    // oldest valid slot (monotonic)

// Pre-built JSON response for /hist.json — no streaming, no heap allocation.
// 480 entries × ~15 chars + header ≈ 7.3 KB; buffer chosen with margin.
#define HIST_JSON_BUFLEN 8192
static char hist_json_buf[HIST_JSON_BUFLEN];

// Accumulator for the minute currently in progress
static float   acc_min    = 1e9f;
static float   acc_max    = -1e9f;
static uint8_t acc_tm_min = 255;

static void history_accumulate(float w, uint8_t tm_min) {
  if (tm_min != acc_tm_min) {
    // Minute boundary — finalise previous entry
    if (acc_tm_min != 255 && acc_max >= acc_min) {
      hist_buf[hist_write_idx % HISTORY_MINUTES] = {acc_min, acc_max};
      hist_write_idx++;
      if (hist_write_idx - hist_read_idx > HISTORY_MINUTES)
        hist_read_idx = hist_write_idx - HISTORY_MINUTES;
    }
    acc_min = acc_max = w;
    acc_tm_min = tm_min;
  } else {
    if (w < acc_min) acc_min = w;
    if (w > acc_max) acc_max = w;
  }
}
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
const uint8_t sml_header[8] = {0x1b, 0x1b, 0x1b, 0x1b, 0x01, 0x01, 0x01, 0x01};

void json_publish(DynamicJsonDocument *json) {
  (*json)["sml_error"]      = sml_error_cnt;
  (*json)["Consumption_Wh"] = consumption_Wh;
  (*json)["Production_Wh"]  = production_Wh;
  (*json)["Power_W"]        = current_W;
  // Publish only the ring-buffer indices; the browser re-fetches /hist.json
  // when hist_write_idx advances (once per minute).  No hist data in the
  // 100 ms broadcast — keeps the WebSocket frame small.
  (*json)["hist_write_idx"] = hist_write_idx;
  (*json)["hist_read_idx"]  = hist_read_idx;
}

void json_update(DynamicJsonDocument *json) {
  (void)json;  // browser sends nothing; history is pushed by server
}

//---------------------------------------------------
#define STACK_SIZE 2000
#define PRIORITY configMAX_PRIORITIES - 1
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

void sml_publish(sml_file *file) {
  for (int i = 0; i < file->messages_len; i++) {
    char name[20];
    sprintf(name, "Name %d", i);
    sml_message *message = file->messages[i];
    //(*json)["valid_sml"] = file->messages_len;
    //(*json)["sml_time"] = buf->receive_time;
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
          // sprintf(buffer, "%s %.*f", obisIdentifier, prec, value);
          //(*json)[name] = buffer;
          if (strcmp(obisIdentifier, "1-0:1.8.0/255") == 0) {
            write_before++;
            consumption_Wh = value;
            write_after = write_before;
          } else if (strcmp(obisIdentifier, "1-0:2.8.0/255") == 0) {
            write_before++;
            production_Wh = value;
            write_after = write_before;
          } else if (strcmp(obisIdentifier, "1-0:16.7.0/255") == 0) {
            if (entry->status &&
                ((*entry->status->data.status16 & 0x20) != 0)) {
              value = -value;
            }
            write_before++;
            current_W = value;
            write_after = write_before;
          }
          //        } else if (entry->value->type == SML_TYPE_OCTET_STRING) {
          //          char *value;
          //          sml_value_to_strhex(entry->value, &value, true);
          //          (*json)[name] = value;
          //          free(value);
          //        } else if (entry->value->type == SML_TYPE_BOOLEAN) {
          //          (*json)[name] = entry->value->data.boolean;
        }

        //        else {
        //          sprintf(buffer, "Unknown %s %d", obisIdentifier,
        //                  entry->value->type & SML_TYPE_FIELD);
        //          (*json)[name] = buffer;
        //        }
      }
    } else {
      //      sprintf(name, "Unknown_tag %d", i);
      //      (*json)[name] = *message->message_body->tag;
    }
  }
}

void SMLTask(void *parameter) {
  while (true) {
    for (uint8_t i = 0; i < 3; i++) {
      struct sml_buffer_s *buf = &sml_buffers[i];
      if (buf->locked || buf->valid_bytes < 0) {
        continue;
      }
      buf->locked = true;
      // read back, if it was really locked
      if (buf->locked) {
        if (buf->valid_bytes >= 0) {
          bool err = true;
          if (buf->valid_bytes > 16) {
            if (memcmp(&buf->data[2], sml_header, 8) == 0) {
              uint16_t chksum =
                  sml_crc16_calculate(&buf->data[2], buf->valid_bytes - 2);
              if (((chksum >> 8) == buf->data[buf->valid_bytes]) &&
                  ((chksum & 0xff) == buf->data[buf->valid_bytes + 1])) {
                sml_file *file =
                    sml_file_parse(&buf->data[2 + 8], buf->valid_bytes - 16);
                sml_publish(file);
                sml_file_free(file);
                err = false;
              }
            }
          }
          if (err) {
            sml_error_cnt++;
          }
          buf->locked = false;
          break;
        }
        buf->locked = false;
      }
    }
    const TickType_t xDelay = 100 / portTICK_PERIOD_MS;
    vTaskDelay(xDelay);
  }
}

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

void DisplayTask(void *parameter) {
  display.init();  // enable diagnostic output on Serial

  display.setRotation(1);
  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);
  display.setCursor(1, 0);
  while (true) {
    display.fillScreen(GxEPD_WHITE);

    display.setCursor(0, 10);

    display.print(WiFi.SSID());
    display.print(" ");
    display.println(WiFi.localIP());

    char strftime_buf[64];
    struct tm timeinfo;
    time_t now = time(nullptr);
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%d.%m.%y, %H:%M ", &timeinfo);
    display.println(strftime_buf);

    char line[64];
    sprintf(line, "Used:   %.3f kWh", consumption_Wh / 1000);
    display.println(line);
    sprintf(line, "Make:   %.3f kWh", production_Wh / 1000);
    display.println(line);
    sprintf(line, "Actual: %.1f W", current_W);
    display.println(line);

    display.setTextColor(GxEPD_BLACK);

    display.setCursor(0, display.height() - 10);

    display.print("SDCard:");
    if (sdOK) {
      uint32_t cardSize = SD.cardSize() / (1024 * 1024);
      uint8_t cardType = SD.cardType();

      if (cardType == CARD_MMC) {
        display.print("MMC ");
      } else if (cardType == CARD_SD) {
        display.print("SDSC ");
      } else if (cardType == CARD_SDHC) {
        display.print("SDHC ");
      } else {
        display.print("UNKNOWN ");
      }
      display.print(cardSize);
      display.println("MB");
    } else {
      display.println("None");
    }

    display.update();
    const TickType_t xDelay = 30000 / portTICK_PERIOD_MS;
    vTaskDelay(xDelay);
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

  // Bulk history endpoint — built into a static global buffer (no streaming,
  // no heap allocation, single send() call — avoids blocking the WebServer task).
  tpl_server.on("/hist.json", HTTP_GET, []() {
    uint32_t ri = hist_read_idx, wi = hist_write_idx;
    int pos = snprintf(hist_json_buf, HIST_JSON_BUFLEN,
                       "{\"ri\":%lu,\"wi\":%lu,\"d\":[", ri, wi);
    for (uint32_t i = ri; i < wi; i++) {
      int room = HIST_JSON_BUFLEN - pos - 4;
      if (room < 20) break;
      uint32_t slot = i % HISTORY_MINUTES;
      pos += snprintf(hist_json_buf + pos, room, "%s[%.0f,%.0f]",
                      i > ri ? "," : "",
                      (double)hist_buf[slot].w_min,
                      (double)hist_buf[slot].w_max);
    }
    hist_json_buf[pos++] = ']';
    hist_json_buf[pos++] = '}';
    hist_json_buf[pos]   = '\0';
    tpl_server.sendHeader("Connection", "close");
    tpl_server.send(200, "application/json", hist_json_buf);
  });

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

  SPI.begin(SPI_CLK, SPI_MISO, SPI_MOSI, ELINK_SS);
  xTaskCreate(DisplayTask, "DisplayTask", STACK_SIZE, NULL, 0, NULL);

  xTaskCreate(SMLTask, "SMLTask", STACK_SIZE, NULL, 0, NULL);

  sdSPI.begin(SDCARD_CLK, SDCARD_MISO, SDCARD_MOSI, SDCARD_SS);

  if (!SD.begin(SDCARD_SS, sdSPI)) {
    sdOK = false;
  } else {
    sdOK = true;
  }

  if (sdOK) {
    tpl_server.serveStatic("/sdcard", SD, "/");
  }

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

  tpl_broadcast_setup();

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
  Serial.println("Setup done.");
}

void log_to_sdcard(struct tm *timeinfo) {
  if ((production_Wh == 0.0) || (consumption_Wh == 0.0)) {
    return;
  }
  char pathname[100];
  size_t n = 0;
  n += sprintf(pathname, "/%d", timeinfo->tm_year + 1900);
  if (!SD.mkdir(pathname)) {
    file_log_error_cnt += 10000;
  }
  n += sprintf(pathname + n, "/%02d", timeinfo->tm_mon + 1);
  if (!SD.mkdir(pathname)) {
    file_log_error_cnt += 1000;
  }
  n += sprintf(pathname + n, "/%02d", timeinfo->tm_mday);
  if (!SD.mkdir(pathname)) {
    file_log_error_cnt += 100;
  }
  n += sprintf(pathname + n, "/%02d", timeinfo->tm_hour);
  if (!SD.mkdir(pathname)) {
    file_log_error_cnt += 10;
  }
  n += sprintf(pathname + n, "/%02d.txt", timeinfo->tm_min);
  File file = SD.open(pathname, FILE_WRITE);
  if (!file) {
    file_log_error_cnt++;
    return;
  }
  char line[255];
  sprintf(line, "Consumption %.1f Wh\nProduction: %.1f Wh\n", consumption_Wh,
          production_Wh);
  file.print(line);
  file.close();
}

uint32_t next_stack_info = 0;
uint32_t no_ip_ms = 0;

uint8_t last_sec = 255;
void loop() {
  uint32_t ms = millis();
  if ((int32_t)(ms - next_stack_info) > 0) {
    next_stack_info = ms + 500;
    tpl_update_stack_info();
    Serial.println(tpl_config.stack_info);
  }

  struct tm timeinfo;
  time_t now = time(nullptr);
  localtime_r(&now, &timeinfo);
  if (timeinfo.tm_sec != last_sec) {
    last_sec = timeinfo.tm_sec;
    history_accumulate(current_W, timeinfo.tm_min);
    twai_message_t message;
    message.flags = TWAI_MSG_FLAG_NONE;
    message.self = 1;
    message.identifier = CAN_ID_TIMESTAMP;
    message.data_length_code = 4;
    message.data[0] = timeinfo.tm_sec;
    message.data[1] = timeinfo.tm_min;
    message.data[2] = timeinfo.tm_hour;
    message.data[3] = timeinfo.tm_wday;
    twai_status_info_t status;
    twai_get_status_info(&status);
    if (status.msgs_to_tx < 5) {
      twai_transmit(&message, portMAX_DELAY);
    } else {
      Serial.println("CAN TX queue full");
    }

    // broadcast - use before/after counters to detect torn reads
    uint32_t before = write_after;
    struct stromzaehler_packet_s packet;
    packet.consumption_Wh = consumption_Wh;
    packet.production_Wh = production_Wh;
    packet.current_W = current_W;
    packet.tm_sec = timeinfo.tm_sec;
    packet.tm_min = timeinfo.tm_min;
    packet.tm_hour = timeinfo.tm_hour;
    packet.tm_wday = timeinfo.tm_wday;
    uint32_t after = write_before;
    if (before == after) {
      // consistent read - no write in progress
      tpl_broadcast((uint8_t *)&packet, sizeof(packet));
    }

    if (timeinfo.tm_sec == 0) {
      Serial.println("log to sdcard");
      log_to_sdcard(&timeinfo);
    }
  }
  // Check if IP is 0.0.0.0
  IPAddress localIP = WiFi.localIP();
  if (localIP == IPAddress(0, 0, 0, 0)) {
    if (ms - no_ip_ms > 5 * 60 * 1000) {
      ESP.restart();
    }
  } else {
    no_ip_ms = ms;
  }

  const TickType_t xDelay = 100 / portTICK_PERIOD_MS;
  vTaskDelay(xDelay);
}
