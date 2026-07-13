#include <stdio.h>
#include "esp_log.h"
#include "camera.h"
#include "localization.h"
#include "wifi_AP.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting application...");
    
    // Bring up WiFi before starting camera/receiver polling tasks
    if (wifi_AP_init() != ESP_OK) {
        ESP_LOGE(TAG, "WiFi AP initialization failed");
        return;
    }

    // Initialize the OV5640 camera and receiver-bus target processing
    camera_init();
    if (localization_init() != ESP_OK) {
        ESP_LOGE(TAG, "Localization initialization failed");
        return;
    }
    
    ESP_LOGI(TAG, "Application started successfully");
}
