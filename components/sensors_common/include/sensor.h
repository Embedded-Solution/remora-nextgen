#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double temperature_c;
    double pressure_bar;
    double depth_m;       // optionnel (si dispo)
    uint64_t ts_us;
} sensor_measure_t;

/** Interface générique (Strategy) */
typedef struct {
    esp_err_t (*init)(void *self);
    esp_err_t (*read)(void *self, sensor_measure_t *out);
    esp_err_t (*sleep)(void *self);
    const char* (*name)(void *self);
    void *self;
} sensor_if_t;

#ifdef __cplusplus
}
#endif
