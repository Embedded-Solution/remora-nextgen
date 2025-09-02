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
    uint8_t addr;           // 0x76 ou 0x77
    // calib PROM (C1..C6), stks internes
    uint16_t C[8];
    uint32_t D1_raw, D2_raw;
    int32_t  dT, TEMP;      // centi-degC
    int64_t  OFF, SENS;
    bool     initialized;
} sensor_ms5837_t;

/** Remplit un sensor_if_t prêt à l'emploi */
void sensor_ms5837_make(i2c_bus_t *bus, uint8_t addr, sensor_if_t *out);

#ifdef __cplusplus
}
#endif
