#ifdef IS_ESP32CAM
#define FROM_TEMPLATE 1
#include "tpl_esp_camera.h"

#include <Arduino.h>
#include <driver/gpio.h>

#include "tpl_system.h"

esp_err_t tpl_camera_setup(uint8_t* fail_cnt, framesize_t frame_size,
                           uint8_t fb_count) {
  *fail_cnt = 0;
  esp_err_t err = ESP_OK;

  // try camera init for max 20 times
  for (uint8_t i = 0; i < 20; i++) {
    camera_config_t camera_config;

    // ensure power off
    if (digitalRead(PWDN_GPIO_NUM) != HIGH) {
      digitalWrite(PWDN_GPIO_NUM, HIGH);
      pinMode(PWDN_GPIO_NUM, OUTPUT);
      delay(1000);
    }
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
    camera_config.pixel_format = PIXFORMAT_JPEG;
    camera_config.frame_size = frame_size;

    // quality of JPEG output. 0-63 lower means higher quality
    camera_config.jpeg_quality = 5;
    // 1: Wait for V-Synch // 2: Continous Capture (Video)
    //   count=3 : one for websocket, one for processing, one for recording
    camera_config.fb_count = fb_count;
    err = esp_camera_init(&camera_config);
    if (err == ESP_OK) {
      *fail_cnt = i;
      sensor_t* sensor = esp_camera_sensor_get();
      sensor->set_hmirror(sensor, 1);
      sensor->set_vflip(sensor, 1);
      return ESP_OK;
    }
    // power off the camera and try again
    digitalWrite(PWDN_GPIO_NUM, HIGH);
    pinMode(PWDN_GPIO_NUM, OUTPUT);
    delay(3000);
  }
  return err;
}

const struct {
  int8_t pin;
  uint8_t state;
} gpios[] = {{Y2_GPIO_NUM, INPUT_PULLDOWN},    {Y3_GPIO_NUM, INPUT_PULLDOWN},
             {Y4_GPIO_NUM, INPUT_PULLDOWN},    {Y5_GPIO_NUM, INPUT_PULLDOWN},
             {Y6_GPIO_NUM, INPUT_PULLDOWN},    {Y7_GPIO_NUM, INPUT_PULLDOWN},
             {Y8_GPIO_NUM, INPUT_PULLDOWN},    {Y9_GPIO_NUM, INPUT_PULLDOWN},
             {XCLK_GPIO_NUM, INPUT_PULLDOWN},  {PCLK_GPIO_NUM, INPUT_PULLDOWN},
             {VSYNC_GPIO_NUM, INPUT_PULLDOWN}, {HREF_GPIO_NUM, INPUT_PULLDOWN},
             {SIOD_GPIO_NUM, INPUT_PULLUP},    {SIOC_GPIO_NUM, INPUT_PULLUP},
             {RESET_GPIO_NUM, INPUT_PULLDOWN}, 127};

void tpl_camera_off() {
  digitalWrite(tpl_ledPin, HIGH);
  digitalWrite(tpl_flashPin, LOW);
  pinMode(tpl_ledPin, INPUT_PULLUP);
  pinMode(tpl_flashPin, INPUT_PULLDOWN);
  if (PWDN_GPIO_NUM >= 0) {
    // power off the camera and try again
    digitalWrite(PWDN_GPIO_NUM, HIGH);
    pinMode(PWDN_GPIO_NUM, OUTPUT);
    for (uint8_t i = 0; gpios[i].pin != 127; i++) {
      gpio_num_t gpio = (gpio_num_t)gpios[i].pin;
      if (gpio >= 0) {
        gpio_reset_pin(gpio);
        pinMode(gpio, gpios[i].state);
      }
    }
  }
}

void tpl_process_web_socket_cam_settings(DynamicJsonDocument* json) {
  if (json->containsKey("brightness")) {
    int8_t brightness = (*json)["brightness"];
    sensor_t* sensor = esp_camera_sensor_get();
    sensor->set_brightness(sensor, brightness);
  }
  if (json->containsKey("contrast")) {
    int8_t contrast = (*json)["contrast"];
    sensor_t* sensor = esp_camera_sensor_get();
    sensor->set_contrast(sensor, contrast);
  }
  if (json->containsKey("saturation")) {
    int8_t saturation = (*json)["saturation"];
    sensor_t* sensor = esp_camera_sensor_get();
    sensor->set_saturation(sensor, saturation);
  }
  if (json->containsKey("ae_level")) {
    int8_t ae_level = (*json)["ae_level"];
    sensor_t* sensor = esp_camera_sensor_get();
    sensor->set_ae_level(sensor, ae_level);
  }
  if (json->containsKey("set_aec_value")) {
    int8_t set_aec_value = (*json)["set_aec_value"];
    sensor_t* sensor = esp_camera_sensor_get();
    sensor->set_aec_value(sensor, set_aec_value);
  }
  if (json->containsKey("gain_ctrl")) {
    int8_t gain_ctrl = (*json)["gain_ctrl"];
    sensor_t* sensor = esp_camera_sensor_get();
    sensor->set_gain_ctrl(sensor, gain_ctrl);
  }
  if (json->containsKey("gainceiling")) {
    int8_t gainceiling = (*json)["gainceiling"];
    sensor_t* sensor = esp_camera_sensor_get();
    sensor->set_gainceiling(sensor, (gainceiling_t)gainceiling);
  }
  if (json->containsKey("bpc")) {
    bool v = (*json)["bpc"];
    sensor_t* sensor = esp_camera_sensor_get();
    sensor->set_bpc(sensor, v ? 1 : 0);
  }
  if (json->containsKey("whitebal")) {
    bool v = (*json)["whitebal"];
    sensor_t* sensor = esp_camera_sensor_get();
    sensor->set_whitebal(sensor, v ? 1 : 0);
  }
  if (json->containsKey("exposure_ctrl")) {
    bool v = (*json)["exposure_ctrl"];
    sensor_t* sensor = esp_camera_sensor_get();
    sensor->set_exposure_ctrl(sensor, v ? 1 : 0);
  }
  if (json->containsKey("awb_gain")) {
    bool v = (*json)["awb_gain"];
    sensor_t* sensor = esp_camera_sensor_get();
    sensor->set_awb_gain(sensor, v ? 1 : 0);
  }
  if (json->containsKey("wpc")) {
    bool v = (*json)["wpc"];
    sensor_t* sensor = esp_camera_sensor_get();
    sensor->set_wpc(sensor, v ? 1 : 0);
  }
  if (json->containsKey("dcw")) {
    bool v = (*json)["dcw"];
    sensor_t* sensor = esp_camera_sensor_get();
    sensor->set_dcw(sensor, v ? 1 : 0);
  }
  if (json->containsKey("raw_gma")) {
    bool v = (*json)["raw_gma"];
    sensor_t* sensor = esp_camera_sensor_get();
    sensor->set_raw_gma(sensor, v ? 1 : 0);
  }
  if (json->containsKey("vflip")) {
    bool v = (*json)["vflip"];
    sensor_t* sensor = esp_camera_sensor_get();
    sensor->set_vflip(sensor, v ? 1 : 0);
  }
  if (json->containsKey("hmirror")) {
    bool v = (*json)["hmirror"];
    sensor_t* sensor = esp_camera_sensor_get();
    sensor->set_hmirror(sensor, v ? 1 : 0);
  }
}

#endif
