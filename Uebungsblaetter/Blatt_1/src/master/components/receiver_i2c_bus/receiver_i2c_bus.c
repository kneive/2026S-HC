#include "receiver_i2c_bus.h"

#include <string.h>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "RECEIVER_I2C_BUS";

#define MUX_I2C_ADDRESS 0x70
#define SLAVE_I2C_ADDRESS 0x12

// Receiver mux wiring. Keep this separate from the OV5640 camera data pins.
#define I2C_MASTER_SDA_IO 1
#define I2C_MASTER_SCL_IO 2
#define I2C_MASTER_FREQ_HZ 100000
#define I2C_MASTER_TIMEOUT_MS 1000

#define PER_DEVICE_BYTES (6 + 1 + 1 + RECEIVER_I2C_BUS_DEVICE_NAME_LEN)
#define PAGE_HEADER_BYTES 5
#define READ_BUFFER_SIZE (PAGE_HEADER_BYTES + (RECEIVER_I2C_BUS_DEVICES_PER_PAGE * PER_DEVICE_BYTES))

static receiver_i2c_bus_device_t receiver_database[RECEIVER_I2C_BUS_ANTENNA_COUNT][RECEIVER_I2C_BUS_MAX_TRACKED_DEVICES];
static uint16_t device_counts_per_antenna[RECEIVER_I2C_BUS_ANTENNA_COUNT] = {0};
static i2c_master_bus_handle_t bus_handle = NULL;
static i2c_master_dev_handle_t mux_dev_handle = NULL;
static i2c_master_dev_handle_t slave_dev_handle = NULL;

static esp_err_t select_mux_channel(uint8_t channel)
{
  if (channel > 7) {
    ESP_LOGW(TAG, "Invalid MUX channel: %d", channel);
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t mux_value = 1 << channel;
  esp_err_t ret = i2c_master_transmit(mux_dev_handle, &mux_value, 1, I2C_MASTER_TIMEOUT_MS);

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to select MUX channel %d: %s", channel, esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGD(TAG, "MUX channel %d selected", channel);
  return ESP_OK;
}

esp_err_t receiver_i2c_bus_init(void)
{
  if (bus_handle != NULL) {
    return ESP_OK;
  }

  ESP_LOGI(TAG, "I2C pins: SDA=%d, SCL=%d", I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);

  if (!GPIO_IS_VALID_GPIO(I2C_MASTER_SDA_IO) || !GPIO_IS_VALID_GPIO(I2C_MASTER_SCL_IO)) {
    ESP_LOGE(TAG, "Invalid SDA/SCL pin number: SDA=%d, SCL=%d", I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
    return ESP_ERR_INVALID_ARG;
  }

  i2c_master_bus_config_t i2c_bus_config = {
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .i2c_port = I2C_NUM_0,
    .scl_io_num = I2C_MASTER_SCL_IO,
    .sda_io_num = I2C_MASTER_SDA_IO,
    .glitch_ignore_cnt = 7,
    .flags.enable_internal_pullup = true,
  };

  ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_bus_config, &bus_handle), TAG, "I2C bus creation failed");

  i2c_device_config_t mux_dev_config = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = MUX_I2C_ADDRESS,
    .scl_speed_hz = I2C_MASTER_FREQ_HZ,
  };

  ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus_handle, &mux_dev_config, &mux_dev_handle),
                      TAG, "MUX device creation failed");

  i2c_device_config_t slave_dev_config = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = SLAVE_I2C_ADDRESS,
    .scl_speed_hz = I2C_MASTER_FREQ_HZ,
  };

  ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus_handle, &slave_dev_config, &slave_dev_handle),
                      TAG, "Slave device creation failed");

  ESP_LOGI(TAG, "Receiver I2C bus initialized (SCL: GPIO%d, SDA: GPIO%d)",
           I2C_MASTER_SCL_IO, I2C_MASTER_SDA_IO);

  return ESP_OK;
}

esp_err_t receiver_i2c_bus_refresh_antenna(uint8_t antenna_id)
{
  if (antenna_id >= RECEIVER_I2C_BUS_ANTENNA_COUNT) {
    ESP_LOGW(TAG, "Invalid antenna ID: %d", antenna_id);
    return ESP_ERR_INVALID_ARG;
  }

  if (bus_handle == NULL) {
    ESP_RETURN_ON_ERROR(receiver_i2c_bus_init(), TAG, "Receiver I2C bus init failed");
  }

  const int select_retries = 3;
  esp_err_t sel_ret = ESP_FAIL;
  for (int attempt = 0; attempt < select_retries; attempt++) {
    sel_ret = select_mux_channel(antenna_id);
    if (sel_ret == ESP_OK) {
      break;
    }
    ESP_LOGW(TAG, "select_mux_channel attempt %d failed for antenna %d: %s",
             attempt + 1, antenna_id, esp_err_to_name(sel_ret));
    vTaskDelay(pdMS_TO_TICKS(50));
  }

  if (sel_ret != ESP_OK) {
    device_counts_per_antenna[antenna_id] = 0;
    ESP_LOGW(TAG, "Skipping antenna %d due to MUX select failure", antenna_id);
    return sel_ret;
  }

  vTaskDelay(pdMS_TO_TICKS(100));

  esp_err_t probe_ret = i2c_master_probe(bus_handle, SLAVE_I2C_ADDRESS, I2C_MASTER_TIMEOUT_MS);
  if (probe_ret != ESP_OK) {
    device_counts_per_antenna[antenna_id] = 0;
    return probe_ret;
  }

  memset(receiver_database[antenna_id], 0, sizeof(receiver_database[antenna_id]));
  device_counts_per_antenna[antenna_id] = 0;

  uint8_t requested_page = 0;
  uint8_t page_count = 1;
  esp_err_t ret = ESP_OK;

  do {
    esp_err_t tx_ret = i2c_master_transmit(slave_dev_handle, &requested_page, 1, I2C_MASTER_TIMEOUT_MS);
    if (tx_ret != ESP_OK) {
      ESP_LOGW(TAG, "Page request transmit failed for MUX channel %d page %d: %s",
               antenna_id, requested_page, esp_err_to_name(tx_ret));
      device_counts_per_antenna[antenna_id] = 0;
      memset(receiver_database[antenna_id], 0, sizeof(receiver_database[antenna_id]));
      return tx_ret;
    }

    uint8_t read_buffer[READ_BUFFER_SIZE] = {0};
    const int read_retries = 3;
    ret = ESP_FAIL;
    for (int attempt = 0; attempt < read_retries; attempt++) {
      ret = i2c_master_receive(slave_dev_handle, read_buffer, READ_BUFFER_SIZE, I2C_MASTER_TIMEOUT_MS);
      if (ret == ESP_OK) {
        break;
      }
      ESP_LOGW(TAG, "Read attempt %d failed for antenna %d page %d: %s",
               attempt + 1, antenna_id, requested_page, esp_err_to_name(ret));
      vTaskDelay(pdMS_TO_TICKS(20));
    }

    if (ret != ESP_OK) {
      device_counts_per_antenna[antenna_id] = 0;
      memset(receiver_database[antenna_id], 0, sizeof(receiver_database[antenna_id]));
      ESP_LOGW(TAG, "Failed to read from antenna %d page %d: %s",
               antenna_id, requested_page, esp_err_to_name(ret));
      return ret;
    }

    uint16_t total_count = (uint16_t)read_buffer[0] | ((uint16_t)read_buffer[1] << 8);
    uint8_t response_page = read_buffer[2];
    page_count = read_buffer[3];
    uint8_t page_device_count = read_buffer[4];
    if (page_device_count > RECEIVER_I2C_BUS_DEVICES_PER_PAGE) {
      ESP_LOGW(TAG, "Slave returned too many devices for one page: %d", page_device_count);
      page_device_count = RECEIVER_I2C_BUS_DEVICES_PER_PAGE;
    }

    if (page_count == 0) {
      page_count = 1;
    }

    if (response_page != requested_page) {
      ESP_LOGW(TAG, "Expected page %d but slave returned page %d", requested_page, response_page);
    }

    if (total_count > RECEIVER_I2C_BUS_MAX_TRACKED_DEVICES) {
      total_count = RECEIVER_I2C_BUS_MAX_TRACKED_DEVICES;
    }

    for (uint8_t i = 0; i < page_device_count; i++) {
      uint16_t device_index = ((uint16_t)requested_page * RECEIVER_I2C_BUS_DEVICES_PER_PAGE) + i;
      if (device_index >= RECEIVER_I2C_BUS_MAX_TRACKED_DEVICES) {
        break;
      }

      int base = PAGE_HEADER_BYTES + (i * PER_DEVICE_BYTES);
      memcpy(receiver_database[antenna_id][device_index].mac, &read_buffer[base], 6);

      uint8_t raw_rssi = read_buffer[base + 6];
      receiver_database[antenna_id][device_index].rssi = -((int)raw_rssi);

      uint8_t name_len = read_buffer[base + 7];
      if (name_len > RECEIVER_I2C_BUS_DEVICE_NAME_LEN) {
        name_len = RECEIVER_I2C_BUS_DEVICE_NAME_LEN;
      }

      memset(receiver_database[antenna_id][device_index].id, 0, RECEIVER_I2C_BUS_DEVICE_NAME_LEN + 1);
      if (name_len > 0) {
        memcpy(receiver_database[antenna_id][device_index].id, &read_buffer[base + 8], name_len);
      }
    }

    device_counts_per_antenna[antenna_id] = total_count;
    requested_page++;
  } while (requested_page < page_count &&
           requested_page * RECEIVER_I2C_BUS_DEVICES_PER_PAGE < RECEIVER_I2C_BUS_MAX_TRACKED_DEVICES);

  ESP_LOGI(TAG, "Antenna %d: %d device(s)", antenna_id, device_counts_per_antenna[antenna_id]);
  for (uint16_t i = 0; i < device_counts_per_antenna[antenna_id]; i++) {
    ESP_LOGD(TAG, "Antenna %d Device %d: %02X:%02X:%02X:%02X:%02X:%02X RSSI: %d Name: %s",
             antenna_id, i,
             receiver_database[antenna_id][i].mac[0], receiver_database[antenna_id][i].mac[1],
             receiver_database[antenna_id][i].mac[2], receiver_database[antenna_id][i].mac[3],
             receiver_database[antenna_id][i].mac[4], receiver_database[antenna_id][i].mac[5],
             receiver_database[antenna_id][i].rssi,
             receiver_database[antenna_id][i].id[0] ? receiver_database[antenna_id][i].id : "<no-name>");
  }

  return ESP_OK;
}

void receiver_i2c_bus_refresh_all(void)
{
  for (uint8_t i = 0; i < RECEIVER_I2C_BUS_ANTENNA_COUNT; i++) {
    (void)receiver_i2c_bus_refresh_antenna(i);
  }
}

uint16_t receiver_i2c_bus_get_device_count(uint8_t antenna_id)
{
  if (antenna_id >= RECEIVER_I2C_BUS_ANTENNA_COUNT) {
    return 0;
  }

  return device_counts_per_antenna[antenna_id];
}

const receiver_i2c_bus_device_t *receiver_i2c_bus_get_device(uint8_t antenna_id, uint16_t device_index)
{
  if (antenna_id >= RECEIVER_I2C_BUS_ANTENNA_COUNT ||
      device_index >= device_counts_per_antenna[antenna_id]) {
    return NULL;
  }

  return &receiver_database[antenna_id][device_index];
}

int receiver_i2c_bus_find_rssi_for_mac(uint8_t antenna_id, const uint8_t *target_mac)
{
  if (antenna_id >= RECEIVER_I2C_BUS_ANTENNA_COUNT || target_mac == NULL) {
    return -100;
  }

  for (uint16_t i = 0; i < device_counts_per_antenna[antenna_id]; i++) {
    if (memcmp(receiver_database[antenna_id][i].mac, target_mac, 6) == 0) {
      return receiver_database[antenna_id][i].rssi;
    }
  }

  return -100;
}

void receiver_i2c_bus_scan(void)
{
  if (bus_handle == NULL && receiver_i2c_bus_init() != ESP_OK) {
    return;
  }

  ESP_LOGI(TAG, "Starting I2C scan on receiver bus");
  for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
    esp_err_t ret = i2c_master_probe(bus_handle, addr, I2C_MASTER_TIMEOUT_MS);
    if (ret == ESP_OK) {
      ESP_LOGI(TAG, "I2C device found at 0x%02X", addr);
    }
  }
  ESP_LOGI(TAG, "I2C scan complete");
}
