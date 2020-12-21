#ifdef IS_ESP32CAM
#include <esp_camera.h>
#include <esp_err.h>
#include <stdint.h>
//#ifdef FROM_TEMPLATE
#include <ArduinoJson.h>
//#endif

#define CONFIG_CAMERA_MODEL_AI_THINKER 1
#include "tpl_app_camera.h"

#ifdef BOTtoken
esp_err_t tpl_camera_setup(uint8_t* fail_cnt,
                           framesize_t frame_size = FRAMESIZE_VGA,
                           fb_count = 1);
#else
esp_err_t tpl_camera_setup(uint8_t* fail_cnt,
                           framesize_t frame_size = FRAMESIZE_VGA,
                           fb_count = 3);
#endif

void tpl_camera_off();
void tpl_process_web_socket_cam_settings(DynamicJsonDocument* json);
#endif
