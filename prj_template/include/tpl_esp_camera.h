/**
 * @file tpl_esp_camera.h
 * @brief ESP32-CAM camera module for ESP32 template
 *
 * Provides camera initialization and control for ESP32-CAM boards.
 * Configured for AI Thinker ESP32-CAM module by default.
 *
 * Features:
 * - Camera initialization with configurable frame size and buffer count
 * - Deep sleep preparation (camera power-off)
 * - WebSocket camera settings control
 *
 * Hardware: AI Thinker ESP32-CAM (CONFIG_CAMERA_MODEL_AI_THINKER)
 *
 * @note Only available when IS_ESP32CAM is defined in build flags.
 */

#ifdef IS_ESP32CAM
#include <esp_camera.h>
#include <esp_err.h>
#include <stdint.h>
// #ifdef FROM_TEMPLATE
#include <ArduinoJson.h>
// #endif

/// Camera model configuration (AI Thinker ESP32-CAM)
#define CONFIG_CAMERA_MODEL_AI_THINKER 1
#include "tpl_app_camera.h"

/**
 * @brief Initialize the ESP32-CAM camera
 *
 * @param fail_cnt Output: Counter for initialization failures
 * @param frame_size Frame size (default: QVGA for Telegram, VGA otherwise)
 * @param fb_count Frame buffer count (default: 1 for Telegram, 3 otherwise)
 * @return ESP_OK on success, error code otherwise
 *
 * @note Uses smaller frame size when Telegram is enabled to reduce memory usage.
 */
#ifdef BOTtoken
esp_err_t tpl_camera_setup(uint8_t* fail_cnt,
                           framesize_t frame_size = FRAMESIZE_QVGA,
                           uint8_t fb_count = 1);
#else
esp_err_t tpl_camera_setup(uint8_t* fail_cnt,
                           framesize_t frame_size = FRAMESIZE_VGA,
                           uint8_t fb_count = 3);
#endif

/**
 * @brief Power off the camera for deep sleep
 *
 * Disables camera power to reduce current consumption during deep sleep.
 */
void tpl_camera_off();

/**
 * @brief Process camera settings from WebSocket JSON
 *
 * Handles camera configuration commands received via WebSocket.
 *
 * @param json JSON document containing camera settings
 */
void tpl_process_web_socket_cam_settings(DynamicJsonDocument* json);
#endif
