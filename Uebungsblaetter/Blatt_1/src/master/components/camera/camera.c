#include "camera.h"

#include <stdbool.h>
#include "driver/gpio.h"
#include "driver/i2c_types.h"
#include "esp_camera.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "CAMERA";

// Default ESP32-S3 OV5640 camera wiring used by common S3 camera boards.
// Adjust these if your board routes the camera differently.
#define CAMERA_PIN_PWDN -1
#define CAMERA_PIN_RESET -1
#define CAMERA_PIN_XCLK 15
#define CAMERA_PIN_SIOD 4
#define CAMERA_PIN_SIOC 5
#define CAMERA_PIN_D7 16
#define CAMERA_PIN_D6 17
#define CAMERA_PIN_D5 18
#define CAMERA_PIN_D4 12
#define CAMERA_PIN_D3 10
#define CAMERA_PIN_D2 8
#define CAMERA_PIN_D1 9
#define CAMERA_PIN_D0 11
#define CAMERA_PIN_VSYNC 6
#define CAMERA_PIN_HREF 7
#define CAMERA_PIN_PCLK 13

#define CAMERA_SCCB_I2C_PORT I2C_NUM_1
#define CAMERA_XCLK_FREQ_HZ 20000000
#define CAMERA_JPEG_QUALITY 12
#define CAMERA_FB_COUNT 1
#define CAMERA_FLIP_VERTICAL 1

static bool camera_driver_initialized = false;
static SemaphoreHandle_t camera_frame_lock = NULL;

static void configure_optional_output_pin(int pin, int level)
{
  if (pin < 0) {
    return;
  }

  gpio_config_t io_conf = {
    .pin_bit_mask = 1ULL << pin,
    .mode = GPIO_MODE_OUTPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_ERROR_CHECK(gpio_config(&io_conf));
  ESP_ERROR_CHECK(gpio_set_level(pin, level));
}

esp_err_t camera_power_on(void)
{
  configure_optional_output_pin(CAMERA_PIN_PWDN, 0);
  configure_optional_output_pin(CAMERA_PIN_RESET, 1);
  vTaskDelay(pdMS_TO_TICKS(10));

  if (CAMERA_PIN_RESET >= 0) {
    ESP_RETURN_ON_ERROR(gpio_set_level(CAMERA_PIN_RESET, 0), TAG, "Camera reset assert failed");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(gpio_set_level(CAMERA_PIN_RESET, 1), TAG, "Camera reset release failed");
  }

  vTaskDelay(pdMS_TO_TICKS(30));
  return ESP_OK;
}

esp_err_t camera_power_off(void)
{
  if (camera_driver_initialized) {
    ESP_RETURN_ON_ERROR(esp_camera_deinit(), TAG, "Camera deinit failed");
    camera_driver_initialized = false;
  }

  configure_optional_output_pin(CAMERA_PIN_PWDN, 1);
  configure_optional_output_pin(CAMERA_PIN_RESET, 0);
  return ESP_OK;
}

static esp_err_t camera_sensor_init(void)
{
  ESP_RETURN_ON_ERROR(camera_power_on(), TAG, "Camera power-on failed");

  camera_config_t config = {
    .pin_pwdn = CAMERA_PIN_PWDN,
    .pin_reset = CAMERA_PIN_RESET,
    .pin_xclk = CAMERA_PIN_XCLK,
    .pin_sccb_sda = CAMERA_PIN_SIOD,
    .pin_sccb_scl = CAMERA_PIN_SIOC,
    .sccb_i2c_port = CAMERA_SCCB_I2C_PORT,
    .pin_d7 = CAMERA_PIN_D7,
    .pin_d6 = CAMERA_PIN_D6,
    .pin_d5 = CAMERA_PIN_D5,
    .pin_d4 = CAMERA_PIN_D4,
    .pin_d3 = CAMERA_PIN_D3,
    .pin_d2 = CAMERA_PIN_D2,
    .pin_d1 = CAMERA_PIN_D1,
    .pin_d0 = CAMERA_PIN_D0,
    .pin_vsync = CAMERA_PIN_VSYNC,
    .pin_href = CAMERA_PIN_HREF,
    .pin_pclk = CAMERA_PIN_PCLK,
    .xclk_freq_hz = CAMERA_XCLK_FREQ_HZ,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,
    .pixel_format = PIXFORMAT_JPEG,
    .frame_size = FRAMESIZE_VGA,
    .jpeg_quality = CAMERA_JPEG_QUALITY,
    .fb_count = CAMERA_FB_COUNT,
    .fb_location = CAMERA_FB_IN_DRAM,
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
  };

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "OV5640 camera init failed: %s", esp_err_to_name(err));
    (void)camera_power_off();
    return err;
  }

  camera_driver_initialized = true;

  sensor_t *sensor = esp_camera_sensor_get();
  if (sensor == NULL) {
    ESP_LOGW(TAG, "Camera initialized, but sensor handle is unavailable");
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Camera sensor PID: 0x%04X", sensor->id.PID);
  if (sensor->set_framesize != NULL) {
    sensor->set_framesize(sensor, FRAMESIZE_VGA);
  }
  if (sensor->set_vflip != NULL) {
    sensor->set_vflip(sensor, CAMERA_FLIP_VERTICAL);
  }

  ESP_LOGI(TAG, "OV5640 camera initialized at VGA JPEG");
  return ESP_OK;
}

camera_fb_t *camera_capture_frame(void)
{
  if (!camera_driver_initialized) {
    ESP_LOGW(TAG, "Frame requested before camera driver initialization");
    return NULL;
  }

  if (camera_frame_lock != NULL) {
    xSemaphoreTake(camera_frame_lock, portMAX_DELAY);
  }

  camera_fb_t *frame = esp_camera_fb_get();
  if (frame == NULL && camera_frame_lock != NULL) {
    xSemaphoreGive(camera_frame_lock);
  }

  return frame;
}

void camera_return_frame(camera_fb_t *frame)
{
  if (frame != NULL) {
    esp_camera_fb_return(frame);
  }

  if (camera_frame_lock != NULL) {
    xSemaphoreGive(camera_frame_lock);
  }
}

void camera_init(void)
{
  ESP_LOGI(TAG, "Initializing OV5640 camera");

  if (camera_sensor_init() != ESP_OK) {
    ESP_LOGE(TAG, "Camera sensor initialization failed");
    return;
  }

  if (camera_frame_lock == NULL) {
    camera_frame_lock = xSemaphoreCreateMutex();
    if (camera_frame_lock == NULL) {
      ESP_LOGE(TAG, "Camera frame mutex creation failed");
      return;
    }
  }

  ESP_LOGI(TAG, "Camera initialized");
}
