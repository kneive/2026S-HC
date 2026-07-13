#include "localization.h"

#include <stdbool.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "receiver_i2c_bus.h"

static const char *TAG = "LOCALIZATION";

#define LOCALIZATION_MAX_TARGET_CANDIDATES 64
#define LOCALIZATION_TASK_STACK_SIZE 8192
#define LOCALIZATION_TASK_PRIORITY 5
#define LOCALIZATION_REFRESH_INTERVAL_MS 50

#define LOCALIZATION_CENTER_X (LOCALIZATION_TARGET_FRAME_WIDTH / 2)
#define LOCALIZATION_CENTER_Y (LOCALIZATION_TARGET_FRAME_HEIGHT / 2)
#define LOCALIZATION_EDGE_X (LOCALIZATION_TARGET_FRAME_WIDTH / 8)
#define LOCALIZATION_EDGE_Y (LOCALIZATION_TARGET_FRAME_HEIGHT / 8)

typedef struct {
  uint8_t mac[6];
  int rssi[RECEIVER_I2C_BUS_ANTENNA_COUNT];
  bool seen[RECEIVER_I2C_BUS_ANTENNA_COUNT];
  int best_rssi;
} target_candidate_t;

static SemaphoreHandle_t localization_targets_lock = NULL;
static TaskHandle_t localization_task_handle = NULL;
static localization_target_t localization_targets[LOCALIZATION_MAX_TARGETS];
static size_t localization_target_count = 0;

/*
 * MUX-channel-to-image calibration.
 *
 * The receiver bus uses antenna_id == mux channel. Keep the physical image
 * positions here so wiring changes do not leak into the localization math.
 *
 * Physical receiver layout:
 *   SD4/SC4: upper right
 *   SD1/SC1: lower right
 *   SD0/SC0: lower left
 *   SD2/SC2: upper left
 *   SD3/SC3: center
 */
static const int antenna_x[RECEIVER_I2C_BUS_ANTENNA_COUNT] = {
  LOCALIZATION_EDGE_X,
  LOCALIZATION_TARGET_FRAME_WIDTH - LOCALIZATION_EDGE_X,
  LOCALIZATION_EDGE_X,
  LOCALIZATION_CENTER_X,
  LOCALIZATION_TARGET_FRAME_WIDTH - LOCALIZATION_EDGE_X,
};

static const int antenna_y[RECEIVER_I2C_BUS_ANTENNA_COUNT] = {
  LOCALIZATION_TARGET_FRAME_HEIGHT - LOCALIZATION_EDGE_Y,
  LOCALIZATION_TARGET_FRAME_HEIGHT - LOCALIZATION_EDGE_Y,
  LOCALIZATION_EDGE_Y,
  LOCALIZATION_CENTER_Y,
  LOCALIZATION_EDGE_Y,
};

size_t localization_get_targets(localization_target_t *targets, size_t max_targets)
{
  if (targets == NULL || max_targets == 0) {
    return 0;
  }

  if (localization_targets_lock != NULL) {
    xSemaphoreTake(localization_targets_lock, portMAX_DELAY);
  }

  size_t count = localization_target_count;
  if (count > max_targets) {
    count = max_targets;
  }

  for (size_t i = 0; i < count; i++) {
    targets[i] = localization_targets[i];
  }

  if (localization_targets_lock != NULL) {
    xSemaphoreGive(localization_targets_lock);
  }

  return count;
}

static void publish_targets(const localization_target_t *targets, size_t count)
{
  if (localization_targets_lock != NULL) {
    xSemaphoreTake(localization_targets_lock, portMAX_DELAY);
  }

  if (count > LOCALIZATION_MAX_TARGETS) {
    count = LOCALIZATION_MAX_TARGETS;
  }

  for (size_t i = 0; i < count; i++) {
    localization_targets[i] = targets[i];
  }
  localization_target_count = count;

  if (localization_targets_lock != NULL) {
    xSemaphoreGive(localization_targets_lock);
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

static void clamp_target_to_frame(localization_target_t *target)
{
  if (target->x < 0) {
    target->x = 0;
  } else if (target->x > LOCALIZATION_TARGET_FRAME_WIDTH) {
    target->x = LOCALIZATION_TARGET_FRAME_WIDTH;
  }

  if (target->y < 0) {
    target->y = 0;
  } else if (target->y > LOCALIZATION_TARGET_FRAME_HEIGHT) {
    target->y = LOCALIZATION_TARGET_FRAME_HEIGHT;
  }
}

static bool calculate_target_position(const target_candidate_t *candidate, localization_target_t *target)
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

static int rssi_for_log(const target_candidate_t *candidate, uint8_t antenna_id)
{
  return candidate->seen[antenna_id] ? candidate->rssi[antenna_id] : -127;
}

static void log_target_position(const target_candidate_t *candidate, const localization_target_t *target)
{
  ESP_LOGD(TAG,
           "MAC: %02X:%02X:%02X:%02X:%02X:%02X RSSI[ch0:%d ch1:%d ch2:%d ch3:%d ch4:%d] -> X:%d Y:%d",
           target->mac[0], target->mac[1], target->mac[2],
           target->mac[3], target->mac[4], target->mac[5],
           rssi_for_log(candidate, 0),
           rssi_for_log(candidate, 1),
           rssi_for_log(candidate, 2),
           rssi_for_log(candidate, 3),
           rssi_for_log(candidate, 4),
           target->x, target->y);
}

static void localization_task(void *arg)
{
  (void)arg;

  while (1) {
    receiver_i2c_bus_refresh_all();

    target_candidate_t candidates[LOCALIZATION_MAX_TARGET_CANDIDATES] = {0};
    size_t candidate_count = 0;
    localization_target_t next_targets[LOCALIZATION_MAX_TARGETS];
    size_t next_target_count = 0;

    for (uint8_t antenna_id = 0; antenna_id < RECEIVER_I2C_BUS_ANTENNA_COUNT; antenna_id++) {
      uint16_t device_count = receiver_i2c_bus_get_device_count(antenna_id);
      for (uint16_t device_index = 0; device_index < device_count; device_index++) {
        const receiver_i2c_bus_device_t *device = receiver_i2c_bus_get_device(antenna_id, device_index);
        if (device == NULL) {
          continue;
        }

        int candidate_index = find_candidate_index(candidates, candidate_count, device->mac);
        if (candidate_index < 0) {
          if (candidate_count >= LOCALIZATION_MAX_TARGET_CANDIDATES) {
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

    for (size_t i = 0; i < candidate_count && next_target_count < LOCALIZATION_MAX_TARGETS; i++) {
      localization_target_t target;
      if (!calculate_target_position(&candidates[i], &target)) {
        continue;
      }

      log_target_position(&candidates[i], &target);
      next_targets[next_target_count++] = target;
    }

    publish_targets(next_targets, next_target_count);
    vTaskDelay(pdMS_TO_TICKS(LOCALIZATION_REFRESH_INTERVAL_MS));
  }
}

esp_err_t localization_init(void)
{
  if (localization_task_handle != NULL) {
    return ESP_OK;
  }

  if (localization_targets_lock == NULL) {
    localization_targets_lock = xSemaphoreCreateMutex();
    if (localization_targets_lock == NULL) {
      ESP_LOGE(TAG, "Target mutex creation failed");
      return ESP_ERR_NO_MEM;
    }
  }

  esp_err_t ret = receiver_i2c_bus_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Receiver I2C bus initialization failed: %s", esp_err_to_name(ret));
    return ret;
  }

  BaseType_t task_ret = xTaskCreate(localization_task,
                                    "localization_task",
                                    LOCALIZATION_TASK_STACK_SIZE,
                                    NULL,
                                    LOCALIZATION_TASK_PRIORITY,
                                    &localization_task_handle);
  if (task_ret != pdPASS) {
    localization_task_handle = NULL;
    ESP_LOGE(TAG, "Localization task creation failed");
    return ESP_ERR_NO_MEM;
  }

  ESP_LOGI(TAG, "Localization task started");
  return ESP_OK;
}
