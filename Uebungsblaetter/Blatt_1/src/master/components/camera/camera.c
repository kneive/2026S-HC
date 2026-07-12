#include <string.h>
#include <stdio.h>
#include "esp_check.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

static const char *TAG = "CAMERA";

#define MUX_I2C_ADDRESS 0x70
#define SLAVE_I2C_ADDRESS 0x12 // same I2C address on every MUX channel
#define MAX_TRACKED_DEVICES 5
#define DEVICE_NAME_LEN 32
#define I2C_MASTER_SCL_IO 9
#define I2C_MASTER_SDA_IO 8
#define I2C_MASTER_FREQ_HZ 100000
#define I2C_MASTER_TIMEOUT_MS 1000

struct AntennaData {
  uint8_t mac[6];
  int rssi;
  char id[DEVICE_NAME_LEN + 1];
};

// 2D matrix to hold data
struct AntennaData MasterDatabase[6][MAX_TRACKED_DEVICES];
uint8_t device_counts_per_antenna[6] = {0, 0, 0, 0, 0, 0};
static i2c_master_bus_handle_t bus_handle = NULL;
static i2c_master_dev_handle_t mux_dev_handle = NULL;
static i2c_master_dev_handle_t slave_dev_handle = NULL;

// I2C initialization
esp_err_t i2c_master_init(void) {
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

  ESP_LOGI(TAG, "I2C Master initialized (SCL: GPIO%d, SDA: GPIO%d)", 
           I2C_MASTER_SCL_IO, I2C_MASTER_SDA_IO);
  
  return ESP_OK;
}

esp_err_t select_mux_channel(uint8_t channel) {
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

void parse_antenna_table(uint8_t antenna_id) {
  if(antenna_id >= 6) {
    ESP_LOGW(TAG, "Invalid antenna ID: %d", antenna_id);
    return;
  }

  // Try selecting MUX channel with retries
  const int select_retries = 3;
  esp_err_t sel_ret = ESP_FAIL;
  for (int attempt = 0; attempt < select_retries; attempt++) {
    sel_ret = select_mux_channel(antenna_id);
    if (sel_ret == ESP_OK) break;
    ESP_LOGW(TAG, "select_mux_channel attempt %d failed for antenna %d: %s", attempt + 1, antenna_id, esp_err_to_name(sel_ret));
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  if (sel_ret != ESP_OK) {
    device_counts_per_antenna[antenna_id] = 0;
    ESP_LOGW(TAG, "Skipping antenna %d due to MUX select failure", antenna_id);
    return;
  }

  // Allow more time for the MUX to switch physically
  vTaskDelay(pdMS_TO_TICKS(100));

  // Debug: selecting MUX channel and querying slave
  // ESP_LOGI(TAG, "Selecting MUX channel %d and querying slave at 0x%02X", antenna_id, SLAVE_I2C_ADDRESS);

  // Probe the slave on this MUX channel before attempting the full read
  esp_err_t probe_ret = i2c_master_probe(bus_handle, SLAVE_I2C_ADDRESS, I2C_MASTER_TIMEOUT_MS);
  if (probe_ret != ESP_OK) {
    device_counts_per_antenna[antenna_id] = 0;
    // Probe failure — comment out verbose warning to reduce log noise
    // ESP_LOGW(TAG, "No slave at 0x%02X on MUX channel %d (probe: %s)", SLAVE_I2C_ADDRESS, antenna_id, esp_err_to_name(probe_ret));
    return;
  }

  // Perform a short write before the read — slave expects master-write then master-read
  uint8_t read_cmd = 0x00;
  esp_err_t tx_ret = i2c_master_transmit(slave_dev_handle, &read_cmd, 1, I2C_MASTER_TIMEOUT_MS);
  if (tx_ret != ESP_OK) {
    ESP_LOGW(TAG, "Pre-read transmit failed for MUX channel %d: %s", antenna_id, esp_err_to_name(tx_ret));
    // continue — read retries may still succeed
  }

  // Request max possible buffer size (1 byte count + MAX_TRACKED_DEVICES * per-device bytes)
  #define PER_DEVICE_BYTES (6 + 1 + 1 + DEVICE_NAME_LEN)
  #define READ_BUFFER_SIZE (1 + (MAX_TRACKED_DEVICES * PER_DEVICE_BYTES))
  uint8_t read_buffer[READ_BUFFER_SIZE];
  // Attempt the read with a few retries to tolerate transient NAKs
  const int read_retries = 3;
  esp_err_t ret = ESP_FAIL;
  for (int attempt = 0; attempt < read_retries; attempt++) {
    ret = i2c_master_receive(slave_dev_handle, read_buffer, READ_BUFFER_SIZE, I2C_MASTER_TIMEOUT_MS);
    if (ret == ESP_OK) break;
    ESP_LOGW(TAG, "Read attempt %d failed for antenna %d: %s", attempt + 1, antenna_id, esp_err_to_name(ret));
    vTaskDelay(pdMS_TO_TICKS(20));
  }

  if(ret == ESP_OK) {
    uint8_t count = read_buffer[0];
    device_counts_per_antenna[antenna_id] = (count < MAX_TRACKED_DEVICES) ? count : MAX_TRACKED_DEVICES;

    for(int i = 0; i < device_counts_per_antenna[antenna_id]; i++) {
      int base = 1 + (i * PER_DEVICE_BYTES);
      // Copy MAC address (6 bytes)
      memcpy(MasterDatabase[antenna_id][i].mac, &read_buffer[base], 6);

      // Copy RSSI (1 byte, convert to negative)
      uint8_t raw_rssi = read_buffer[base + 6];
      MasterDatabase[antenna_id][i].rssi = -((int)raw_rssi);

      // Copy device name using length prefix (NAMELEN at base+7)
      uint8_t name_len = read_buffer[base + 7];
      if (name_len > DEVICE_NAME_LEN) name_len = DEVICE_NAME_LEN;
      memset(MasterDatabase[antenna_id][i].id, 0, DEVICE_NAME_LEN + 1);
      if (name_len) memcpy(MasterDatabase[antenna_id][i].id, &read_buffer[base + 8], name_len);
    }

    // Output received data at INFO level so user can see what was read (includes device name)
    ESP_LOGI(TAG, "Antenna %d: %d device(s)", antenna_id, device_counts_per_antenna[antenna_id]);
    for (int i = 0; i < device_counts_per_antenna[antenna_id]; i++) {
      ESP_LOGI(TAG, "Antenna %d Device %d: %02X:%02X:%02X:%02X:%02X:%02X RSSI: %d Name: %s",
               antenna_id, i,
               MasterDatabase[antenna_id][i].mac[0], MasterDatabase[antenna_id][i].mac[1],
               MasterDatabase[antenna_id][i].mac[2], MasterDatabase[antenna_id][i].mac[3],
               MasterDatabase[antenna_id][i].mac[4], MasterDatabase[antenna_id][i].mac[5],
               MasterDatabase[antenna_id][i].rssi,
               MasterDatabase[antenna_id][i].id[0] ? MasterDatabase[antenna_id][i].id : "<no-name>");
    }
    #undef PER_DEVICE_BYTES
    #undef READ_BUFFER_SIZE
  } else {
    device_counts_per_antenna[antenna_id] = 0;  // Antenna offline
    ESP_LOGW(TAG, "Failed to read from antenna %d: %s", antenna_id, esp_err_to_name(ret));
  }
}

// Helper - lookup what a specific antenna measured for a specific MAC Address
int find_rssi_for_mac(uint8_t antenna_id, uint8_t* target_mac) {
  if(antenna_id >= 6) return -100;
  
  for(int i = 0; i < device_counts_per_antenna[antenna_id]; i++) {
    if(memcmp(MasterDatabase[antenna_id][i].mac, target_mac, 6) == 0) {
      return MasterDatabase[antenna_id][i].rssi;
    }
  }
  return -100;  // Not seen by this antenna
}

// Main camera processing task
void camera_task(void *arg) {
  while(1) {
    // 1. Grab databases from all 6 antenna channels
    for(uint8_t i = 0; i < 6; i++) {
      parse_antenna_table(i);
    }

    // 2. Use Center Antenna (ID 4) as baseline loop list
    for(int i = 0; i < device_counts_per_antenna[4]; i++) {
      uint8_t* current_mac = MasterDatabase[4][i].mac;
      int rssi_center = MasterDatabase[4][i].rssi;

      // Find matching signals on outer quadrants
      int rssi_top = find_rssi_for_mac(0, current_mac);
      int rssi_bottom = find_rssi_for_mac(1, current_mac);
      int rssi_left = find_rssi_for_mac(2, current_mac);
      int rssi_right = find_rssi_for_mac(3, current_mac);

      // Only triangulate if all quadrants caught a piece of the transmission
      if(rssi_top > -95 && rssi_bottom > -95 && rssi_left > -95 && rssi_right > -95) {
        float dev_x = (float)(rssi_right - rssi_left) / (float)abs(rssi_center);
        float dev_y = (float)(rssi_top - rssi_bottom) / (float)abs(rssi_center);
        int pixel_x = (320 + (int)(dev_x * 450.0) < 0) ? 0 : ((320 + (int)(dev_x * 450.0) > 640) ? 640 : 320 + (int)(dev_x * 450.0));
        int pixel_y = (240 - (int)(dev_y * 450.0) < 0) ? 0 : ((240 - (int)(dev_y * 450.0) > 480) ? 480 : 240 - (int)(dev_y * 450.0));

        // Print coordinate calculations for every active device found
        ESP_LOGI(TAG, "MAC: %02X:%02X:%02X:%02X:%02X:%02X -> Target Pixels X: %d | Y: %d",
          current_mac[0], current_mac[1], current_mac[2],
          current_mac[3], current_mac[4], current_mac[5], pixel_x, pixel_y);

        // Video Code comes here
      }
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// I2C bus scanner: probes addresses and logs any responding devices
static void i2c_scan_bus(void)
{
  // Scanner debug output commented out to reduce log noise. Enable if needed for troubleshooting.
  // ESP_LOGI(TAG, "Starting I2C scan on bus...");
  // for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
  //   esp_err_t ret = i2c_master_probe(bus_handle, addr, I2C_MASTER_TIMEOUT_MS);
  //   if (ret == ESP_OK) {
  //     ESP_LOGI(TAG, "I2C device found at 0x%02X", addr);
  //   }
  // }
  // ESP_LOGI(TAG, "I2C scan complete");
}

// Initialization function
void camera_init(void) {
  ESP_LOGI(TAG, "Initializing camera module...");
  
  if(i2c_master_init() != ESP_OK) {
    ESP_LOGE(TAG, "I2C Master initialization failed");
    return;
  }
  // Run a brief I2C scan to list any devices on the bus (helps debug wiring)
  i2c_scan_bus();

  // Create camera processing task
  xTaskCreate(camera_task, "camera_task", 4096, NULL, 5, NULL);
  ESP_LOGI(TAG, "Camera task started");
}
