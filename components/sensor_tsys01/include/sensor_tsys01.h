#pragma once
#include "sensor.h"
#include "i2c_bus.h"
#include "esp_err.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    i2c_bus_t *bus;
    uint8_t addr;        // typ. 0x77
    uint16_t C[8];       // coefficients PROM
    bool initialized;
} sensor_tsys01_t;

/** Remplit un sensor_if_t prêt à l'emploi */
void sensor_tsys01_make(i2c_bus_t *bus, uint8_t addr, sensor_if_t *out);

#ifdef __cplusplus
}
#endif
