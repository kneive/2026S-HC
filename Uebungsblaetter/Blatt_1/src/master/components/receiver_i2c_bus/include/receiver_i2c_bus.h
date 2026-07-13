#ifndef RECEIVER_I2C_BUS_H
#define RECEIVER_I2C_BUS_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RECEIVER_I2C_BUS_ANTENNA_COUNT 5
#define RECEIVER_I2C_BUS_MAX_TRACKED_DEVICES 256
#define RECEIVER_I2C_BUS_DEVICES_PER_PAGE 6
#define RECEIVER_I2C_BUS_DEVICE_NAME_LEN 32

typedef struct {
  uint8_t mac[6];
  int rssi;
  char id[RECEIVER_I2C_BUS_DEVICE_NAME_LEN + 1];
} receiver_i2c_bus_device_t;

esp_err_t receiver_i2c_bus_init(void);
esp_err_t receiver_i2c_bus_refresh_antenna(uint8_t antenna_id);
void receiver_i2c_bus_refresh_all(void);
uint16_t receiver_i2c_bus_get_device_count(uint8_t antenna_id);
const receiver_i2c_bus_device_t *receiver_i2c_bus_get_device(uint8_t antenna_id, uint16_t device_index);
int receiver_i2c_bus_find_rssi_for_mac(uint8_t antenna_id, const uint8_t *target_mac);
void receiver_i2c_bus_scan(void);

#ifdef __cplusplus
}
#endif

#endif
