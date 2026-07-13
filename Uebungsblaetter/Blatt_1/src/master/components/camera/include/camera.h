#ifndef CAMERA_H
#define CAMERA_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_camera.h"

#define CAMERA_MAX_TARGETS 30
#define CAMERA_TARGET_FRAME_WIDTH 640
#define CAMERA_TARGET_FRAME_HEIGHT 480

typedef struct {
  uint8_t mac[6];
  int x;
  int y;
  int rssi;
} camera_target_t;

/**
 * @brief Initialize the OV5640 camera and start target processing.
 */
void camera_init(void);

esp_err_t camera_power_on(void);
esp_err_t camera_power_off(void);
camera_fb_t *camera_capture_frame(void);
void camera_return_frame(camera_fb_t *frame);
size_t camera_get_targets(camera_target_t *targets, size_t max_targets);

#endif // CAMERA_H
