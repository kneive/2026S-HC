#ifndef CAMERA_H
#define CAMERA_H

#include "esp_err.h"
#include "esp_camera.h"

/**
 * @brief Initialize the OV5640 camera.
 */
void camera_init(void);

esp_err_t camera_power_on(void);
esp_err_t camera_power_off(void);
camera_fb_t *camera_capture_frame(void);
void camera_return_frame(camera_fb_t *frame);

#endif // CAMERA_H
