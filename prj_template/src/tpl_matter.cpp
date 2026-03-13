/**
 * @file tpl_matter.cpp
 * @brief Matter protocol integration implementation
 */

#ifdef USE_TPL_MATTER

#include <WiFi.h>
#include <nvs_flash.h>

#include "tpl_matter.h"
#include "tpl_system.h"
#include "wifi_secrets.h"

#ifdef USE_TPL_MATTER_CUSTOM_PAIRING
#include "matter_pairing.h"
#endif

tpl_matter_state_s tpl_matter_state;
TaskHandle_t task_matter = nullptr;
static int tpl_matter_mode = TPL_MATTER_STANDALONE;

static esp_err_t store_pairing_data(uint16_t discriminator, uint32_t passcode) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("chip-tool-config", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        Serial.printf("Matter: Failed to open NVS: %d\n", err);
        return err;
    }

    err = nvs_set_u16(handle, "discriminator", discriminator);
    if (err != ESP_OK) {
        Serial.printf("Matter: Failed to set discriminator: %d\n", err);
        nvs_close(handle);
        return err;
    }

    err = nvs_set_u32(handle, "passcode", passcode);
    if (err != ESP_OK) {
        Serial.printf("Matter: Failed to set passcode: %d\n", err);
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    nvs_close(handle);
    
    Serial.printf("Matter: Stored pairing (disc=%u, pass=%u)\n", discriminator, passcode);
    return ESP_OK;
}

static void update_pairing_info() {
    String code = ArduinoMatter::getManualPairingCode();
    String url = ArduinoMatter::getOnboardingQRCodeUrl();
    strncpy(tpl_matter_state.manual_pairing_code, code.c_str(),
            sizeof(tpl_matter_state.manual_pairing_code) - 1);
    strncpy(tpl_matter_state.qr_code_url, url.c_str(),
            sizeof(tpl_matter_state.qr_code_url) - 1);
}

void TaskMatter(void* pvParameters) {
    const TickType_t xDelay = 500 / portTICK_PERIOD_MS;

    for (;;) {
        tpl_matter_state.commissioned = ArduinoMatter::isDeviceCommissioned();
        tpl_matter_state.connected = ArduinoMatter::isDeviceConnected();
        tpl_matter_state.wifi_connected = ArduinoMatter::isWiFiConnected();

        if (tpl_matter_state.connected) {
            tpl_matter_state.last_ok_ms = millis();
        }

        vTaskDelay(xDelay);
    }
}

static bool connect_wifi_sync() {
    Serial.println("Matter: Connecting to WiFi...");

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);

    const struct net_s* net = &nets[0];
    int attempts = 0;
    const int max_attempts = 3;

    while (attempts < max_attempts) {
        net = &nets[0];
        while (net->ssid) {
            Serial.printf("Matter: Trying %s...\n", net->ssid);
            WiFi.begin(net->ssid, net->passwd);

            int wait = 0;
            while (WiFi.status() != WL_CONNECTED && wait < 100) {
                delay(100);
                wait++;
                if (wait % 10 == 0) {
                    Serial.print(".");
                }
            }

            if (WiFi.status() == WL_CONNECTED) {
                Serial.println();
                Serial.printf("Matter: WiFi connected to %s\n", net->ssid);
                Serial.print("Matter: IP address: ");
                Serial.println(WiFi.localIP());
                tpl_matter_state.wifi_connected = true;
                return true;
            }

            WiFi.disconnect(true);
            delay(500);
            net++;
        }
        attempts++;
        Serial.printf("Matter: WiFi attempt %d/%d failed\n", attempts, max_attempts);
    }

    Serial.println("Matter: Failed to connect to WiFi");
    return false;
}

static bool wait_for_tpl_wifi(uint32_t timeout_ms) {
    Serial.println("Matter: Waiting for tpl_wifi connection...");
    uint32_t start = millis();

    while (WiFi.status() != WL_CONNECTED) {
        if (timeout_ms > 0 && (millis() - start) > timeout_ms) {
            Serial.println("Matter: Timeout waiting for tpl_wifi");
            return false;
        }
        delay(100);
    }

    Serial.print("Matter: WiFi ready, IP: ");
    Serial.println(WiFi.localIP());
    return true;
}

void tpl_matter_setup(int mode) {
    tpl_matter_mode = mode;
    memset(&tpl_matter_state, 0, sizeof(tpl_matter_state));
    tpl_matter_state.last_ok_ms = millis();

#ifdef USE_TPL_MATTER_CUSTOM_PAIRING
    uint32_t passcode = 0;
    const char* passcode_str = MATTER_PASSCODE;
    passcode = atol(passcode_str);
    
    Serial.printf("Matter: Setting custom pairing (disc=%u, pass=%s)\n", 
                  MATTER_DISCRIMINATOR, passcode_str);
    store_pairing_data(MATTER_DISCRIMINATOR, passcode);
#endif

    bool wifi_ready = false;

    if (mode == TPL_MATTER_STANDALONE) {
        wifi_ready = connect_wifi_sync();
    } else {
        wifi_ready = wait_for_tpl_wifi(30000);
    }

    if (!wifi_ready) {
        Serial.println("Matter: No WiFi, proceeding anyway (BLE commissioning may work)");
    }

    update_pairing_info();

    ArduinoMatter::begin();

    tpl_matter_state.commissioned = ArduinoMatter::isDeviceCommissioned();
    tpl_matter_state.connected = ArduinoMatter::isDeviceConnected();

    Serial.println("Matter: Stack started");

    xTaskCreatePinnedToCore(TaskMatter, "Matter", 4096, nullptr, 1, &task_matter, CORE_0);
}

bool tpl_matter_wait_commissioned(uint32_t timeout_ms) {
    if (tpl_matter_state.commissioned) {
        return true;
    }

    Serial.println("\nMatter: Node not commissioned");
    Serial.println("Initiate device discovery in your Matter environment");
    Serial.printf("Manual pairing code: %s\n", tpl_matter_state.manual_pairing_code);
    Serial.printf("QR code URL: %s\n", tpl_matter_state.qr_code_url);

    uint32_t start = millis();
    uint32_t last_print = 0;

    while (!tpl_matter_state.commissioned) {
        if (timeout_ms > 0 && (millis() - start) > timeout_ms) {
            Serial.println("Matter: Commissioning timeout");
            return false;
        }

        if (millis() - last_print > 5000) {
            Serial.println("Matter: Waiting for commissioning...");
            last_print = millis();
        }
        delay(100);
    }

    Serial.println("Matter: Commissioned and ready");
    return true;
}

void tpl_matter_decommission() {
    Serial.println("Matter: Decommissioning...");
    ArduinoMatter::decommission();
    tpl_matter_state.commissioned = false;
}

#endif
