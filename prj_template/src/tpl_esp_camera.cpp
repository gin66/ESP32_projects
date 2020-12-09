#ifdef IS_ESP32CAM
#include "tpl_esp_camera.h"

#include <Arduino.h>

volatile bool camera_in_use = false;

esp_err_t tpl_init_camera(uint8_t* fail_cnt, bool grayscale) {
  *fail_cnt = 0;
  esp_err_t err = ESP_OK;
  if (camera_in_use) {
    return ESP_OK;
  }
  // try camera init for max 20 times
  for (uint8_t i = 0; i < 20; i++) {
    camera_config_t camera_config;

    // Turn camera power on and wait 100ms
    digitalWrite(PWDN_GPIO_NUM, LOW);
    pinMode(PWDN_GPIO_NUM, OUTPUT);
    delay(100);

    camera_config.ledc_channel = LEDC_CHANNEL_0;
    camera_config.ledc_timer = LEDC_TIMER_0;
    camera_config.pin_d0 = Y2_GPIO_NUM;
    camera_config.pin_d1 = Y3_GPIO_NUM;
    camera_config.pin_d2 = Y4_GPIO_NUM;
    camera_config.pin_d3 = Y5_GPIO_NUM;
    camera_config.pin_d4 = Y6_GPIO_NUM;
    camera_config.pin_d5 = Y7_GPIO_NUM;
    camera_config.pin_d6 = Y8_GPIO_NUM;
    camera_config.pin_d7 = Y9_GPIO_NUM;
    camera_config.pin_xclk = XCLK_GPIO_NUM;
    camera_config.pin_pclk = PCLK_GPIO_NUM;
    camera_config.pin_vsync = VSYNC_GPIO_NUM;
    camera_config.pin_href = HREF_GPIO_NUM;
    camera_config.pin_sscb_sda = SIOD_GPIO_NUM;
    camera_config.pin_sscb_scl = SIOC_GPIO_NUM;
    // Do not let the driver to operate PWDN_GPIO_NUM;
    camera_config.pin_pwdn = -1;  // PWDN_GPIO_NUM;
    camera_config.pin_reset = RESET_GPIO_NUM;
    camera_config.xclk_freq_hz = 10000000;
    if (grayscale) {
      // higher resolution leads to artefacts
      camera_config.pixel_format = PIXFORMAT_GRAYSCALE;
      camera_config.frame_size = FRAMESIZE_QVGA;
    } else {
      camera_config.pixel_format = PIXFORMAT_JPEG;
      camera_config.frame_size = FRAMESIZE_VGA;
    }

    // quality of JPEG output. 0-63 lower means higher quality
    camera_config.jpeg_quality = 5;
    // 1: Wait for V-Synch // 2: Continous Capture (Video)
    camera_config.fb_count = 1;
    err = esp_camera_init(&camera_config);
    if (err == ESP_OK) {
      *fail_cnt = i;
      sensor_t* sensor = esp_camera_sensor_get();
      sensor->set_hmirror(sensor, 1);
      sensor->set_vflip(sensor, 1);
      camera_in_use = true;
      return ESP_OK;
    }
    // power off the camera and try again
    digitalWrite(PWDN_GPIO_NUM, HIGH);
    pinMode(PWDN_GPIO_NUM, OUTPUT);
    delay(3000);
  }
  return err;
}
#endif
