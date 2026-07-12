#include <stdio.h>
#include "esp_log.h"
#include "camera.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting application...");
    
    // Initialize camera module with I2C and antenna processing
    camera_init();
    
    ESP_LOGI(TAG, "Application started successfully");
}