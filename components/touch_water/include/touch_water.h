#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "sdkconfig.h"
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif

esp_err_t touch_water_init(void);
void      touch_water_deinit(void);
bool      touch_water_is_present(void);
esp_err_t touch_water_prepare_wakeup(void);
uint32_t  touch_water_get_baseline(void);
uint32_t  touch_water_get_threshold(void);
uint32_t  touch_water_read_raw(void);

#ifdef __cplusplus
}
#endif
