/*
  ESP32-CYD LVGL Boilerplate
  ==========================

  Ivan Tarozzi (itarozzi@gmail.com) 2024




  Lib dependencies
  -----------------

  - "XPT2046_Touchscreen" library by Paul Stoffregen to use the Touchscreen - https://github.com/PaulStoffregen/XPT2046_Touchscreen 
  - "TFT_eSPI" library by Bodmer to use the TFT display - https://github.com/Bodmer/TFT_eSPI
  - "lvgl" library by LVGL - https://github.com/lvgl/lvgl
*/

#include <Arduino.h>

#define SW_NAME_REV "19_CYD-project v1.0"

//************* software serial pins used for debug (if serial0 is used for communication) *************
#define RXPIN 27
#define TXPIN 22

//************* ESP32_projects template includes  *************
#include "template.h"

//************* lvgl and UI includes  *************
#include <lvgl.h>
#include <stdlib.h>  // For abs()
#include "ui/ui.h"
#include "ui/vars.h"
#include "ui/screens.h"  // Need this for objects_t
//#include "ui/styles.h"
#include "ui/actions.h"
//#include "ui/images.h"

// Power chart module
#include "power_chart.h"

//************* TFT display and includes  *************

#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

// Touchscreen pins
#define XPT2046_IRQ 36   // T_IRQ
#define XPT2046_MOSI 32  // T_DIN
#define XPT2046_MISO 39  // T_OUT
#define XPT2046_CLK 25   // T_CLK
#define XPT2046_CS 33    // T_CS

SPIClass touchscreenSPI = SPIClass(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

// Touchscreen coordinates: (x, y) and pressure (z)
int x, y, z;

// Backlight control
#define BACKLIGHT_DIM 51      // 20% brightness
#define BACKLIGHT_FULL 255     // 100% brightness
#define BACKLIGHT_TIMEOUT_MS 60000  // 1 minute
#define BACKLIGHT_FADE_UP_MS 500    // 0.5s to go from dim to full
#define BACKLIGHT_FADE_DOWN_MS 10000 // 10s to go from full to dim
unsigned long g_last_touch_time = 0;
unsigned long g_fade_start_time = 0;
uint8_t g_backlight_current = BACKLIGHT_DIM;
uint8_t g_backlight_target = BACKLIGHT_DIM;
uint8_t g_backlight_fade_start = BACKLIGHT_DIM;

#define DRAW_BUF_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT / 10 * (LV_COLOR_DEPTH / 8))
uint32_t draw_buf[DRAW_BUF_SIZE / 4];


// TFT display
TFT_eSPI tft = TFT_eSPI();
// ******************



// ** Define your action here **

// If you define custom action to change pages, then you can use these

// void action_goto_page2(lv_event_t * e){
//   loadScreen(SCREEN_ID_PAGE1);
// }
//
// void action_goto_homek(lv_event_t * e){
//   loadScreen(SCREEN_ID_MAIN);
// }


// ** Define your vars getter and setter here, if native variables are used in your project **

// bool is_led_active = false;
//
// bool get_var_led_active()
// {
//   return is_led_active;
// }
//
// void set_var_led_active(bool value) {
//   is_led_active = value;
//
//   // Cheap Yellow Display built-in RGB LED is controlled with inverted logic
//   digitalWrite(CYD_LED_BLUE, value ? LOW : HIGH);
// }

// Broadcast receiver variables and functions
stromzaehler_packet_s g_stromzaehler_data;
unsigned long g_last_broadcast_time = 0;

// Function to update power display labels
static void update_power_labels(void) {
    char buffer[16];
    
    if (objects.pow_current != NULL) {
        float current = power_chart_get_current();
        snprintf(buffer, sizeof(buffer), "%.0f", current);
        lv_label_set_text(objects.pow_current, buffer);
    }
    
    if (objects.pow_min != NULL) {
        float min = power_chart_get_min();
        snprintf(buffer, sizeof(buffer), "%.0f", min);
        lv_label_set_text(objects.pow_min, buffer);
    }
    
    if (objects.pow_max != NULL) {
        float max = power_chart_get_max();
        snprintf(buffer, sizeof(buffer), "%.0f", max);
        lv_label_set_text(objects.pow_max, buffer);
    }
    
    if (objects.consumedk_wh != NULL) {
        float consumed_kWh = g_stromzaehler_data.consumption_Wh / 1000.0f;
        snprintf(buffer, sizeof(buffer), "%.3f kWh", consumed_kWh);
        lv_label_set_text(objects.consumedk_wh, buffer);
    }
    
    if (objects.producedk_wh != NULL) {
        float produced_kWh = g_stromzaehler_data.production_Wh / 1000.0f;
        snprintf(buffer, sizeof(buffer), "%.3f kWh", produced_kWh);
        lv_label_set_text(objects.producedk_wh, buffer);
    }
}

void check_broadcast() {
    stromzaehler_packet_s packet;
    size_t received_size;
    
    if (tpl_broadcast_receive(&packet, sizeof(packet), &received_size)) {
        if (received_size == sizeof(stromzaehler_packet_s)) {
            g_last_broadcast_time = millis();
            memcpy(&g_stromzaehler_data, &packet, sizeof(stromzaehler_packet_s));
            
            // Output to Serial
            Serial.printf("[Broadcast] Time: %02d:%02d:%02d, Current: %.1fW, Consumption: %.1fWh, Production: %.1fWh\n",
                         packet.tm_hour, packet.tm_min, packet.tm_sec,
                         packet.current_W, packet.consumption_Wh, packet.production_Wh);
            
            // Update power chart with current power value
            power_chart_add_value(packet.current_W);
            
            // Update power display labels
            update_power_labels();
        } else {
            Serial.printf("[Broadcast] Received %zu bytes, expected %zu\n", 
                         received_size, sizeof(stromzaehler_packet_s));
        }
    }
    
    // Print status every 10 seconds if no recent broadcast
    static unsigned long last_status_time = 0;
    unsigned long now = millis();
    if (now - last_status_time > 10000) {
        last_status_time = now;
        if (g_last_broadcast_time > 0 && now - g_last_broadcast_time < 30000) {
            Serial.printf("[Status] Last broadcast: %lu seconds ago\n", (now - g_last_broadcast_time) / 1000);
        } else {
            Serial.println("[Status] Waiting for broadcast messages...");
        }
    }
}


// If logging is enabled, it will inform the user about what is happening in the library
void log_print(lv_log_level_t level, const char * buf) {
  LV_UNUSED(level);
  Serial.println(buf);
  Serial.flush();
}

// Initialize power display labels with default values
static void init_power_labels(void) {
    Serial.println("[Main] Initializing power labels...");
    
    if (objects.pow_current != NULL) {
        lv_label_set_text(objects.pow_current, "0.0");
        Serial.println("[Main] Initialized pow_current label");
    } else {
        Serial.println("[Main] ERROR: objects.pow_current is NULL!");
    }
    
    if (objects.pow_min != NULL) {
        lv_label_set_text(objects.pow_min, "0.0");
        Serial.println("[Main] Initialized pow_min label");
    } else {
        Serial.println("[Main] ERROR: objects.pow_min is NULL!");
    }
    
    if (objects.pow_max != NULL) {
        lv_label_set_text(objects.pow_max, "0.0");
        Serial.println("[Main] Initialized pow_max label");
    } else {
        Serial.println("[Main] ERROR: objects.pow_max is NULL!");
    }
}

// Initialize chart system after UI is created
static void init_power_chart_system() {
    Serial.println("[Chart] Initializing power chart system...");
    
    Serial.printf("[Chart] Checking objects.power_chart: %p\n", objects.power_chart);
    
    if (objects.power_chart != NULL) {
        Serial.println("[Chart] Found power_chart object from eeZ-Studio");
        
        // Initialize power chart system
        power_chart_init();
        power_chart_set_chart_obj(objects.power_chart);
        
        Serial.println("[Chart] Power chart system initialized successfully");
    } else {
        Serial.println("[Chart] ERROR: objects.power_chart is NULL!");
        Serial.println("[Chart] Creating a new chart object as fallback...");
        
        if (objects.main != NULL) {
            // Create a new chart object as fallback
            lv_obj_t* chart_obj = lv_chart_create(objects.main);
            if (chart_obj == NULL) {
                Serial.println("[Chart] ERROR: Failed to create new chart object!");
                return;
            }
            
            // Set position and size to match the expected chart from screens.c
            lv_obj_set_pos(chart_obj, 9, 11);
            lv_obj_set_size(chart_obj, 251, 100);
            
            // Initialize power chart system
            power_chart_init();
            power_chart_set_chart_obj(chart_obj);
            
            Serial.println("[Chart] Created new chart object as fallback");
        } else {
            Serial.println("[Chart] ERROR: objects.main is also NULL!");
        }
    }
}

// Get the Touchscreen data
void touchscreen_read(lv_indev_t * indev, lv_indev_data_t * data) {
  // Checks if Touchscreen was touched, and prints X, Y and Pressure (Z)
  if(touchscreen.tirqTouched() && touchscreen.touched()) {
    // Get Touchscreen points
    TS_Point p = touchscreen.getPoint();
    // Calibrate Touchscreen points with map function to the correct width and height
    x = map(p.x, 200, 3700, 1, SCREEN_WIDTH);
    y = map(p.y, 240, 3800, 1, SCREEN_HEIGHT);
    z = p.z;

    data->state = LV_INDEV_STATE_PRESSED;

    // Set the coordinates
    data->point.x = x;
    data->point.y = y;

    // Trigger fade to full brightness on touch
    g_last_touch_time = millis();
    if (g_backlight_target != BACKLIGHT_FULL) {
      g_backlight_fade_start = g_backlight_current;
      g_backlight_target = BACKLIGHT_FULL;
      g_fade_start_time = millis();
    }
  }
  else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

void backlight_update() {
  unsigned long now = millis();

  // Check timeout: start fading down after 1 minute of no touch
  if (g_backlight_target == BACKLIGHT_FULL && (now - g_last_touch_time > BACKLIGHT_TIMEOUT_MS)) {
    g_backlight_fade_start = g_backlight_current;
    g_backlight_target = BACKLIGHT_DIM;
    g_fade_start_time = now;
  }

  // Handle fade animation
  if (g_backlight_current != g_backlight_target) {
    unsigned long fade_duration = (g_backlight_target == BACKLIGHT_FULL) ? BACKLIGHT_FADE_UP_MS : BACKLIGHT_FADE_DOWN_MS;
    unsigned long elapsed = now - g_fade_start_time;

    if (elapsed >= fade_duration) {
      g_backlight_current = g_backlight_target;
    } else {
      int16_t delta = (int16_t)g_backlight_target - (int16_t)g_backlight_fade_start;
      g_backlight_current = g_backlight_fade_start + (delta * elapsed / fade_duration);
    }
    ledcWrite(7, g_backlight_current);
  }

  // Debug output
  if (objects.debug != NULL) {
    static uint8_t last_debug_value = 0;
    if (g_backlight_current != last_debug_value) {
      last_debug_value = g_backlight_current;
      char buf[32];
      snprintf(buf, sizeof(buf), "BL: %d", g_backlight_current);
      lv_label_set_text(objects.debug, buf);
    }
  }
}


// lvgl initialization for esp32 board
void lv_init_esp32(void) {

  // Register print function for debugging
  lv_log_register_print_cb(log_print);

  // Start the SPI for the touchscreen and init the touchscreen
  touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  
  touchscreen.begin(touchscreenSPI);
  // Set the Touchscreen rotation in portrait mode
  touchscreen.setRotation(3);  // 2:vertical / 3:horizontal

  // Create a display object
  lv_display_t * disp;
  // Initialize the TFT display using the TFT_eSPI library
  disp = lv_tft_espi_create(SCREEN_WIDTH, SCREEN_HEIGHT, draw_buf, sizeof(draw_buf));
  
  // set rotation mode
  tft.setRotation(3);  // 0 or 2 for  portrait / 1 or 3 for landscape


  // Initialize an LVGL input device object (Touchscreen)
  lv_indev_t * indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  // Set the callback function to read Touchscreen input
  lv_indev_set_read_cb(indev, touchscreen_read);


  // you can define TFT_INVERTED as compiler param in platformio.ini
  #ifdef TFT_INVERTED
    tft.invertDisplay(true);
  #else
    tft.invertDisplay(false);
  #endif

}

void setup() {
  String LVGL_Arduino = String("LVGL Library Version: ") + lv_version_major() + "." + lv_version_minor() + "." + lv_version_patch();

  Serial.begin(115200);
  //Serial.begin(115200, SERIAL_8N1, RXPIN, TXPIN);
  
  Serial.println(SW_NAME_REV);
  Serial.println(LVGL_Arduino);

  pinMode(tpl_ledPin_R, OUTPUT);
  pinMode(tpl_ledPin_G, OUTPUT);
  pinMode(tpl_ledPin_B, OUTPUT);

  // Turn off CYD RGB LED
  digitalWrite(tpl_ledPin_B, HIGH);
  digitalWrite(tpl_ledPin_G, HIGH);
  digitalWrite(tpl_ledPin_R, HIGH);

  // Initialize ESP32_projects template system
  tpl_system_setup(0);  // no deep sleep
  tpl_wifi_setup(true, true, (gpio_num_t)255);
  tpl_webserver_setup();
  tpl_websocket_setup(NULL, NULL);
  tpl_broadcast_setup();
  tpl_command_setup(NULL);

  // Start LVGL
  lv_init();

  // Init TFT and Touch for esp32
  lv_init_esp32();

  // Initialize backlight PWM after TFT init (channel 7 to avoid TFT_eSPI conflicts)
  ledcSetup(7, 5000, 8);
  ledcAttachPin(tpl_backlightPin, 7);
  ledcWrite(7, BACKLIGHT_DIM);

  // Integrate EEZ Studio GUI
  ui_init();
  Serial.println("[Main] UI initialized");
  
  // Initialize power chart system (must be after UI is initialized)
  Serial.println("[Main] Initializing power chart system...");
  init_power_chart_system();
  
  // Initialize power labels with default values
  init_power_labels();
  
  Serial.println("[Main] Setup complete");
}

void loop() {

  static long last_ms = 0;

  long now_ms = millis();

  // LVGL GUI tasks
  lv_task_handler();  // let the GUI do its work
  lv_tick_inc(now_ms - last_ms);     // tell LVGL how much time has passed

  ui_tick(); // uncomment if using eez-flow

  // Check for broadcast messages
  check_broadcast();

  // Check backlight timeout
  backlight_update();

  last_ms = now_ms;
  delay(5);           // let this time pass
}
