#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t rgb_led_init(void);
/** r,g,b = 0..255 (à 8 bits). */
esp_err_t rgb_led_set_rgb(uint8_t r, uint8_t g, uint8_t b);
/** h: 0..359 (°), s:0..255, v:0..255 */
esp_err_t rgb_led_set_hsv(uint16_t h, uint8_t s, uint8_t v);

void rgb_led_deinit(void);

#ifdef __cplusplus
}
#endif
