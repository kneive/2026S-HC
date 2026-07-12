#ifndef CAMERA_H
#define CAMERA_H

#include <stdint.h>

/**
 * @brief Initialize the camera module with I2C master configuration
 * Starts the camera task for antenna data collection and triangulation
 */
void camera_init(void);

#endif // CAMERA_H
