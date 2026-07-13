#include "camera.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/i2c_types.h"
#include "esp_camera.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "receiver_i2c_bus.h"

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
static SemaphoreHandle_t camera_targets_lock = NULL;
static camera_target_t camera_targets[CAMERA_MAX_TARGETS];
static size_t camera_target_count = 0;

typedef struct {
  uint8_t mac[6];
  int rssi[RECEIVER_I2C_BUS_ANTENNA_COUNT];
  bool seen[RECEIVER_I2C_BUS_ANTENNA_COUNT];
  int best_rssi;
} target_candidate_t;

static const int antenna_x[RECEIVER_I2C_BUS_ANTENNA_COUNT] = {
  CAMERA_TARGET_FRAME_WIDTH / 2,
  CAMERA_TARGET_FRAME_WIDTH / 2,
  CAMERA_TARGET_FRAME_WIDTH / 8,
  (CAMERA_TARGET_FRAME_WIDTH * 7) / 8,
  CAMERA_TARGET_FRAME_WIDTH / 2,
};

static const int antenna_y[RECEIVER_I2C_BUS_ANTENNA_COUNT] = {
  CAMERA_TARGET_FRAME_HEIGHT / 8,
  (CAMERA_TARGET_FRAME_HEIGHT * 7) / 8,
  CAMERA_TARGET_FRAME_HEIGHT / 2,
  CAMERA_TARGET_FRAME_HEIGHT / 2,
  CAMERA_TARGET_FRAME_HEIGHT / 2,
};

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

size_t camera_get_targets(camera_target_t *targets, size_t max_targets)
{
  if (targets == NULL || max_targets == 0) {
    return 0;
  }

  if (camera_targets_lock != NULL) {
    xSemaphoreTake(camera_targets_lock, portMAX_DELAY);
  }

  size_t count = camera_target_count;
  if (count > max_targets) {
    count = max_targets;
  }

  for (size_t i = 0; i < count; i++) {
    targets[i] = camera_targets[i];
  }

  if (camera_targets_lock != NULL) {
    xSemaphoreGive(camera_targets_lock);
  }

  return count;
}

static void publish_targets(const camera_target_t *targets, size_t count)
{
  if (camera_targets_lock != NULL) {
    xSemaphoreTake(camera_targets_lock, portMAX_DELAY);
  }

  if (count > CAMERA_MAX_TARGETS) {
    count = CAMERA_MAX_TARGETS;
  }

  for (size_t i = 0; i < count; i++) {
    camera_targets[i] = targets[i];
  }
  camera_target_count = count;

  if (camera_targets_lock != NULL) {
    xSemaphoreGive(camera_targets_lock);
  }
}

static int find_candidate_index(const target_candidate_t *candidates, size_t count, const uint8_t *mac)
{
  for (size_t i = 0; i < count; i++) {
    if (memcmp(candidates[i].mac, mac, 6) == 0) {
      return (int)i;
    }
  }

  return -1;
}

static int rssi_to_weight(int rssi)
{
  int weight = 100 + rssi;
  return (weight > 1) ? weight : 1;
}

static void clamp_target_to_frame(camera_target_t *target)
{
  if (target->x < 0) {
    target->x = 0;
  } else if (target->x > CAMERA_TARGET_FRAME_WIDTH) {
    target->x = CAMERA_TARGET_FRAME_WIDTH;
  }

  if (target->y < 0) {
    target->y = 0;
  } else if (target->y > CAMERA_TARGET_FRAME_HEIGHT) {
    target->y = CAMERA_TARGET_FRAME_HEIGHT;
  }
}

static bool calculate_target_position(const target_candidate_t *candidate, camera_target_t *target)
{
  int weighted_x = 0;
  int weighted_y = 0;
  int total_weight = 0;

  for (uint8_t antenna_id = 0; antenna_id < RECEIVER_I2C_BUS_ANTENNA_COUNT; antenna_id++) {
    if (!candidate->seen[antenna_id]) {
      continue;
    }

    int weight = rssi_to_weight(candidate->rssi[antenna_id]);
    weighted_x += antenna_x[antenna_id] * weight;
    weighted_y += antenna_y[antenna_id] * weight;
    total_weight += weight;
  }

  if (total_weight == 0) {
    return false;
  }

  memcpy(target->mac, candidate->mac, sizeof(target->mac));
  target->x = weighted_x / total_weight;
  target->y = weighted_y / total_weight;
  target->rssi = candidate->best_rssi;
  clamp_target_to_frame(target);

  return true;
}

static void camera_task(void *arg)
{
  (void)arg;

  while (1) {
    receiver_i2c_bus_refresh_all();

    target_candidate_t candidates[CAMERA_MAX_TARGETS] = {0};
    size_t candidate_count = 0;
    camera_target_t next_targets[CAMERA_MAX_TARGETS];
    size_t next_target_count = 0;

    for (uint8_t antenna_id = 0; antenna_id < RECEIVER_I2C_BUS_ANTENNA_COUNT; antenna_id++) {
      uint8_t device_count = receiver_i2c_bus_get_device_count(antenna_id);
      for (uint8_t device_index = 0; device_index < device_count; device_index++) {
        const receiver_i2c_bus_device_t *device = receiver_i2c_bus_get_device(antenna_id, device_index);
        if (device == NULL) {
          continue;
        }

        int candidate_index = find_candidate_index(candidates, candidate_count, device->mac);
        if (candidate_index < 0) {
          if (candidate_count >= CAMERA_MAX_TARGETS) {
            continue;
          }

          candidate_index = (int)candidate_count++;
          memcpy(candidates[candidate_index].mac, device->mac, sizeof(candidates[candidate_index].mac));
          candidates[candidate_index].best_rssi = device->rssi;
        }

        candidates[candidate_index].seen[antenna_id] = true;
        candidates[candidate_index].rssi[antenna_id] = device->rssi;
        if (device->rssi > candidates[candidate_index].best_rssi) {
          candidates[candidate_index].best_rssi = device->rssi;
        }
      }
    }

    for (size_t i = 0; i < candidate_count && next_target_count < CAMERA_MAX_TARGETS; i++) {
      camera_target_t target;
      if (!calculate_target_position(&candidates[i], &target)) {
        continue;
      }

      ESP_LOGI(TAG, "MAC: %02X:%02X:%02X:%02X:%02X:%02X -> Target Pixels X: %d | Y: %d",
               target.mac[0], target.mac[1], target.mac[2],
               target.mac[3], target.mac[4], target.mac[5],
               target.x, target.y);

      next_targets[next_target_count++] = target;
    }

    publish_targets(next_targets, next_target_count);
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void camera_init(void)
{
  ESP_LOGI(TAG, "Initializing receiver bus and OV5640 camera");

  if (camera_sensor_init() != ESP_OK) {
    ESP_LOGE(TAG, "Camera sensor initialization failed");
    return;
  }

  if (receiver_i2c_bus_init() != ESP_OK) {
    ESP_LOGE(TAG, "Receiver I2C bus initialization failed");
    return;
  }

  if (camera_frame_lock == NULL) {
    camera_frame_lock = xSemaphoreCreateMutex();
    if (camera_frame_lock == NULL) {
      ESP_LOGE(TAG, "Camera frame mutex creation failed");
      return;
    }
  }

  if (camera_targets_lock == NULL) {
    camera_targets_lock = xSemaphoreCreateMutex();
    if (camera_targets_lock == NULL) {
      ESP_LOGE(TAG, "Camera target mutex creation failed");
      return;
    }
  }

  xTaskCreate(camera_task, "camera_task", 8192, NULL, 5, NULL);
  ESP_LOGI(TAG, "Camera task started");
}
