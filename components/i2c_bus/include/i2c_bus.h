#pragma once
#include "driver/i2c.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct i2c_bus i2c_bus_t;

/** Crée et initialise un bus I²C (avec mutex interne) */
esp_err_t i2c_bus_create(i2c_port_t port, gpio_num_t sda, gpio_num_t scl, uint32_t hz, i2c_bus_t **out);

/** Détruit le bus */
void i2c_bus_destroy(i2c_bus_t *bus);

/** Write Read atomique avec retries + timeout */
esp_err_t i2c_bus_write_read(i2c_bus_t *bus, uint8_t addr,
                             const uint8_t *w, size_t wl,
                             uint8_t *r, size_t rl,
                             TickType_t timeout_ticks);

/** Write seul */
static inline esp_err_t i2c_bus_write(i2c_bus_t *bus, uint8_t addr,
                                      const uint8_t *w, size_t wl, TickType_t to)
{
    return i2c_bus_write_read(bus, addr, w, wl, NULL, 0, to);
}

#ifdef __cplusplus
}
#endif
