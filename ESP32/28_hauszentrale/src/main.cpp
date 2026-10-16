#include "template.h"
#include <esp_heap_caps.h>
#include <esp_flash.h>

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("28_hauszentrale S1.01 OK");

    tpl_system_setup(0);  // no deep sleep

    size_t psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    Serial.printf("Flash: %d MB, PSRAM: %s\n",
                  flash_size / 1024 / 1024,
                  psramFound() ? "8 MB" : "0 MB");

    tpl_wifi_setup(true, true, (gpio_num_t)255);  // no status LED on this board
    tpl_webserver_setup();
    tpl_websocket_setup(NULL, NULL);
#ifdef NET_WATCHDOG
    tpl_net_watchdog_setup();
#endif
    tpl_command_setup(NULL);
}

void loop() {
    vTaskDelay(10 / portTICK_PERIOD_MS);
}
