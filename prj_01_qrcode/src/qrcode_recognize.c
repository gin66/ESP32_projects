/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2017 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>
 *
 * Permission is hereby granted for use on ESPRESSIF SYSTEMS products only, in
 * which case, it is free of charge, to any person obtaining a copy of this
 * software and associated documentation files (the "Software"), to deal in the
 * Software without restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */
//#include <stdio.h>
#include "qrcode_recognize.h"

#include <string.h>

#include "esp_camera.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/portmacro.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "quirc.h"
#include "quirc_internal.h"

#define PRINT_QR 1

static char *TAG = "QR-recognizer";

static const char *data_type_str(int dt) {
  switch (dt) {
    case QUIRC_DATA_TYPE_NUMERIC:
      return "NUMERIC";
    case QUIRC_DATA_TYPE_ALPHA:
      return "ALPHA";
    case QUIRC_DATA_TYPE_BYTE:
      return "BYTE";
    case QUIRC_DATA_TYPE_KANJI:
      return "KANJI";
  }
  return "unknown";
}

static void dump_cells(const struct quirc_code *code) {
  int u = 0, v = 0;

  printf("    %d cells, corners:", code->size);
  for (u = 0; u < 4; u++) {
    printf(" (%d,%d)", code->corners[u].x, code->corners[u].y);
  }
  printf("\n");

  for (v = 0; v < code->size; v++) {
    printf("\033[0m    ");
    for (u = 0; u < code->size; u++) {
      int p = v * code->size + u;

      if (code->cell_bitmap[p >> 3] & (1 << (p & 7))) {
        printf("\033[40m  ");
      } else {
        printf("\033[47m  ");
      }
    }
    printf("\033[0m\n");
  }
}

static void dump_data(const struct quirc_data *data) {
  printf("    Version: %d\n", data->version);
  printf("    ECC level: %c\n", "MLHQ"[data->ecc_level]);
  printf("    Mask: %d\n", data->mask);
  printf("    Data type: %d (%s)\n", data->data_type,
         data_type_str(data->data_type));
  printf("    Length: %d\n", data->payload_len);
  printf("\033[31m    Payload: %s\n", data->payload);

  if (data->eci) {
    printf("\033[31m    ECI: %d\n", data->eci);
  }
  printf("\033[0m\n");
}

static void dump_info(struct quirc *q, uint8_t count) {
  printf("%d QR-codes found:\n\n", count);
  for (int i = 0; i < count; i++) {
    struct quirc_code code;
    struct quirc_data data;

    // Extract the QR-code specified by the given index.
    quirc_extract(q, i, &code);

    // Decode a QR-code, returning the payload data.
    quirc_decode_error_t err = quirc_decode(&code, &data);

#if PRINT_QR
    dump_cells(&code);
    printf("\n");
#endif

    if (err) {
      printf("  Decoding FAILED: %s\n", quirc_strerror(err));
    } else {
      printf("  Decoding successful:\n");
      printf("    %d cells, corners:", code.size);
      for (uint8_t u = 0; u < 4; u++) {
        printf(" (%d,%d)", code.corners[u].x, code.corners[u].y);
      }
      printf("\n");
      dump_data(&data);
    }
    printf("\n");
  }
}

void _qr_recognize(void *unused) {
  ESP_LOGE(TAG, "qr rec heap: %u", xPortGetFreeHeapSize());
//  sensor_t *sensor = esp_camera_sensor_get();
//  sensor->set_pixformat(sensor, PIXFORMAT_GRAYSCALE);
//  sensor->set_framesize(sensor, FRAMESIZE_QVGA);
//  camera_config_t *camera_config = (camera_config_t *)parameter;
// Use QVGA Size currently, but quirc can support other frame size.(eg:
// FRAMESIZE_QVGA,FRAMESIZE_HQVGA,FRAMESIZE_QCIF,FRAMESIZE_QQVGA2,FRAMESIZE_QQVGA,etc)
#ifdef NEW
//  if (camera_config->frame_size > FRAMESIZE_SVGA) {
//    ESP_LOGE(TAG, "Camera Frame Size err %d, support maxsize is QVGA",
//             (camera_config->frame_size));
//    vTaskDelete(NULL);
//  }
#endif

  struct quirc *qr_recognizer = NULL;
  camera_fb_t *fb = NULL;
  uint8_t *image = NULL;
  int id_count = 0;

  // Construct a new QR-code recognizer.
  ESP_LOGI(TAG, "Construct a new QR-code recognizer(quirc).");
  qr_recognizer = quirc_new();
  if (!qr_recognizer) {
    ESP_LOGE(TAG, "Can't create quirc object");
  }

  ESP_LOGE(TAG, "alloc qr heap: %u", xPortGetFreeHeapSize());
  ESP_LOGE(TAG, "uxHighWaterMark = %d", uxTaskGetStackHighWaterMark(NULL));

  ESP_LOGE(TAG, "uxHighWaterMark = %d", uxTaskGetStackHighWaterMark(NULL));
  // Capture a frame
  fb = esp_camera_fb_get();
  ESP_LOGE(TAG, "image taken heap: %u", xPortGetFreeHeapSize());
  if (!fb) {
    ESP_LOGE(TAG, "Camera capture failed");
  } else {
    ESP_LOGE(TAG, "Recognizer size change w h len: %d, %d, %d", fb->width,
             fb->height, fb->len);
    ESP_LOGE(TAG, "Resize the QR-code recognizer.");
    // Resize the QR-code recognizer.
    if (quirc_resize(qr_recognizer, fb->width, fb->height) < 0) {
      ESP_LOGE(TAG,
               "Resize the QR-code recognizer err (cannot allocate memory).");
    } else {
      ESP_LOGE(TAG, "resized");

      /** These functions are used to process images for QR-code recognition.
       * quirc_begin() must first be called to obtain access to a buffer into
       * which the input image should be placed. Optionally, the current
       * width and height may be returned.
       *
       * After filling the buffer, quirc_end() should be called to process
       * the image for QR-code recognition. The locations and content of each
       * code may be obtained using accessor functions described below.
       */
      image = quirc_begin(qr_recognizer, NULL, NULL);
      ESP_LOGE(TAG, "after begin");
      memcpy(image, fb->buf, fb->width * fb->height);
      ESP_LOGE(TAG, "after memcpy");
      quirc_end(qr_recognizer);
      ESP_LOGE(TAG, "after end");

      // Return the number of QR-codes identified in the last processed image.
      id_count = quirc_count(qr_recognizer);
      if (id_count == 0) {
        ESP_LOGE(TAG, "Error: not a valid qrcode");
      } else {
        // Print information of QR-code
        dump_info(qr_recognizer, id_count);
      }
    }
    esp_camera_fb_return(fb);
  }

  // Destroy QR-Code recognizer (quirc)
  quirc_destroy(qr_recognizer);
  ESP_LOGE(TAG, "Deconstruct QR-Code recognizer(quirc)");
  vTaskDelete(NULL);
}

void app_qr_recognize() {
  xTaskCreate(_qr_recognize, "_qr_recognize", 1024 * 100, NULL, 5, NULL);
}
