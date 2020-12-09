#ifdef IS_ESP32CAM
#include <esp_camera.h>
#include <esp_err.h>
#include <stdint.h>

#define CONFIG_CAMERA_MODEL_AI_THINKER 1
#include "tpl_app_camera.h"

extern volatile bool camera_in_use;
esp_err_t tpl_init_camera(uint8_t* fail_cnt, bool grayscale);
void tpl_camera_off();
#endif
