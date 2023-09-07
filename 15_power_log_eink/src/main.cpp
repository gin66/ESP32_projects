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
#include <driver/twai.h>
#include <esp_task_wdt.h>
#include <base64.h>
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

struct sml_buffer_s {
  int16_t valid_bytes;
  volatile bool locked;
  uint8_t data[258];  // including length bytes
} sml_buffers[3] = {
	{ .valid_bytes = -1, .locked = false },
	{ .valid_bytes = -1, .locked = false },
	{ .valid_bytes = -1, .locked = false },
};

  void publish(DynamicJsonDocument *json, sml_file *file)
  {
	(*json)["in_publish"] = true;
    for (int i = 0; i < file->messages_len; i++)
    {
		  char name[20];
		  sprintf(name, "Name %d", i);
      sml_message *message = file->messages[i];
      if (*message->message_body->tag == SML_MESSAGE_GET_LIST_RESPONSE)
      {
        sml_list *entry;
        sml_get_list_response *body;
        body = (sml_get_list_response *)message->message_body->data;
		uint8_t index;
        for (entry = body->val_list,index = 0; entry != NULL; entry = entry->next,index++)
        {
		  sprintf(name, "Name %d-%d", i,index);
          if (!entry->value)
          { // do not crash on null value
            continue;
          }

          char obisIdentifier[32];
          char buffer[255];

          sprintf(obisIdentifier, "%d-%d:%d.%d.%d/%d",
                  entry->obj_name->str[0], entry->obj_name->str[1],
                  entry->obj_name->str[2], entry->obj_name->str[3],
                  entry->obj_name->str[4], entry->obj_name->str[5]);


          if (((entry->value->type & SML_TYPE_FIELD) == SML_TYPE_INTEGER) ||
              ((entry->value->type & SML_TYPE_FIELD) == SML_TYPE_UNSIGNED))
          {
            double value = sml_value_to_double(entry->value);
            int scaler = (entry->scaler) ? *entry->scaler : 0;
            int prec = -scaler;
            if (prec < 0)
              prec = 0;
            value = value * pow(10, scaler);
            sprintf(buffer, "%s %.*f", obisIdentifier, prec, value);
			(*json)[name] = buffer;
          }
		  else {
            sprintf(buffer, "Unknown %s", obisIdentifier);
			(*json)[name] = buffer;
		  }
        }
      }
	  else {
		  sprintf(name, "Unknown_tag %d", i);
		  (*json)[name] = *message->message_body->tag;
	  }
    }
  }

const uint8_t sml_header[8] = {
	0x1b, 0x1b, 0x1b, 0x1b,
	0x01, 0x01, 0x01, 0x01
};

void json_publish(DynamicJsonDocument *json) {
	(*json)["publish"] = "called";
	for (uint8_t i = 0;i < 3;i++) {
      struct sml_buffer_s *buf = &sml_buffers[i];
	  if (buf->locked || buf->valid_bytes < 0) {
		  continue;
	  }
	  buf->locked = true;
	  // read back, if it was really locked
	  if (buf->locked) {
		  if (buf->valid_bytes >= 0) {
			(*json)["can_b64"] = base64::encode(&buf->data[2], buf->valid_bytes);
			if (buf->valid_bytes > 16) {
				if (memcmp(&buf->data[2], sml_header, 8) == 0) {
				  sml_file *file = sml_file_parse(&buf->data[2+8], buf->valid_bytes - 16);
				  (*json)["valid_sml"] = file->messages_len;
				  publish(json, file);
				  sml_file_free(file);
				}
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
        }
			// new sml message
			// find buffer
			for (uint8_t i = 0;i < 3;i++) {
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
    }
    if (alerts_triggered & TWAI_ALERT_BUS_ERROR) {
      Serial.println(
          "Alert: A (Bit, Stuff, CRC, Form, ACK) error has occurred on the "
          "bus.");
      Serial.printf("Bus error count: %d\n", twaistatus.bus_error_count);
    }
    if (alerts_triggered & TWAI_ALERT_RX_QUEUE_FULL) {
      Serial.println(
          "Alert: The RX queue is full causing a received frame to be lost.");
      Serial.printf("RX buffered: %d\t", twaistatus.msgs_to_rx);
      Serial.printf("RX missed: %d\t", twaistatus.rx_missed_count);
      Serial.printf("RX overrun %d\n", twaistatus.rx_overrun_count);
    }

    // Check if message is received
    if (alerts_triggered & TWAI_ALERT_RX_DATA) {
      // One or more messages received. Handle all.
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

  SPI.begin(SPI_CLK, SPI_MISO, SPI_MOSI, ELINK_SS);
  display.init();  // enable diagnostic output on Serial

  display.setRotation(1);
  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);
  display.setCursor(0, 0);

  sdSPI.begin(SDCARD_CLK, SDCARD_MISO, SDCARD_MOSI, SDCARD_SS);

  if (!SD.begin(SDCARD_SS, sdSPI)) {
    sdOK = false;
  } else {
    sdOK = true;
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

  // Reconfigure alerts to detect frame receive, Bus-Off error and RX queue
  // full states
  uint32_t alerts_to_enable = TWAI_ALERT_RX_DATA | TWAI_ALERT_ERR_PASS |
                              TWAI_ALERT_BUS_ERROR | TWAI_ALERT_RX_QUEUE_FULL;
  if (twai_reconfigure_alerts(alerts_to_enable, NULL) == ESP_OK) {
    Serial.println("CAN Alerts reconfigured");
    xTaskCreate(CANTask, "CANTask", STACK_SIZE, NULL, PRIORITY, NULL);
  } else {
    Serial.println("Failed to reconfigure alerts");
    return;
  }
  Serial.println("Setup done.");
}

void update_display() {
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
}

uint8_t last_sec = 255;
void loop() {
  struct tm timeinfo;
  time_t now = time(nullptr);
  localtime_r(&now, &timeinfo);
  if (timeinfo.tm_sec != last_sec) {
    last_sec = timeinfo.tm_sec;
    twai_message_t message;
    message.flags = TWAI_MSG_FLAG_NONE;
    message.self = 1;
    message.identifier = CAN_ID_TIMESTAMP;
    message.data_length_code = 4;
    message.data[0] = timeinfo.tm_sec;
    message.data[1] = timeinfo.tm_min;
    message.data[2] = timeinfo.tm_hour;
    message.data[3] = timeinfo.tm_wday;
    twai_transmit(&message, portMAX_DELAY);

    if (timeinfo.tm_sec == 0) {
      Serial.println("update display");
      update_display();
    }
  }
  const TickType_t xDelay = 100 / portTICK_PERIOD_MS;
  vTaskDelay(xDelay);
}
