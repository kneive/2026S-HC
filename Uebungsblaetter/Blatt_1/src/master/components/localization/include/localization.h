#ifndef LOCALIZATION_H
#define LOCALIZATION_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LOCALIZATION_MAX_TARGETS 30
#define LOCALIZATION_TARGET_FRAME_WIDTH 640
#define LOCALIZATION_TARGET_FRAME_HEIGHT 480

typedef struct {
  uint8_t mac[6];
  int x;
  int y;
  int rssi;
} localization_target_t;

esp_err_t localization_init(void);
size_t localization_get_targets(localization_target_t *targets, size_t max_targets);

#ifdef __cplusplus
}
#endif

#endif // LOCALIZATION_H
