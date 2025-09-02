#include "sensor_tsys01.h"
#include "esp_log.h"
#include "esp_check.h" 
#include "esp_timer.h"
#include "esp_random.h" 
#include <string.h>
#include <math.h>

static const char *TAG = "TSYS01";

/* Commandes TSYS01 */
#define CMD_RESET           0x1E
#define CMD_ADC_READ        0x00
#define CMD_ADC_TEMP_CONV   0x48
#define CMD_PROM_READ       0xA0

#ifndef CONFIG_TSYS01_I2C_ADDR
#define CONFIG_TSYS01_I2C_ADDR 0x77
#endif

/* ---------- Simulation ---------- */
#if CONFIG_TSYS01_SIMULATION
static double rand01(void) {
    uint32_t r = esp_random();
    return (double)r / (double)UINT32_MAX;
}
static double rand_in(double mn, double mx) {
    if (mx < mn) { double t = mn; mn = mx; mx = t; }
    return mn + (mx - mn) * rand01();
}
#endif

/* ---------- I2C helpers ---------- */
static esp_err_t ts_cmd(sensor_tsys01_t *s, uint8_t cmd) {
    return i2c_bus_write(s->bus, s->addr, &cmd, 1, pdMS_TO_TICKS(20));
}
static esp_err_t ts_read_prom(sensor_tsys01_t *s) {
    for (int i = 0; i < 8; ++i) {
        uint8_t cmd = CMD_PROM_READ + (i * 2);
        uint8_t r[2] = {0};
        esp_err_t e = i2c_bus_write_read(s->bus, s->addr, &cmd, 1, r, 2, pdMS_TO_TICKS(20));
        if (e != ESP_OK) return e;
        s->C[i] = ((uint16_t)r[0] << 8) | r[1];
        // ESP_LOGD(TAG, "C[%d]=%u", i, s->C[i]);
    }
    return ESP_OK;
}
static esp_err_t ts_read_temp_raw(sensor_tsys01_t *s, uint32_t *out) {
    ESP_RETURN_ON_ERROR(ts_cmd(s, CMD_ADC_TEMP_CONV), TAG, "start conv");
    vTaskDelay(pdMS_TO_TICKS(10)); // temps de conversion
    uint8_t rcmd = CMD_ADC_READ;
    uint8_t b[3] = {0};
    ESP_RETURN_ON_ERROR(i2c_bus_write_read(s->bus, s->addr, &rcmd, 1, b, 3, pdMS_TO_TICKS(20)), TAG, "read");
    *out = ((uint32_t)b[0] << 16) | ((uint32_t)b[1] << 8) | b[2];
    /* la lib Arduino divisait par 256 avant polynôme */
    *out = *out / 256u;
    return ESP_OK;
}

/* ---------- Interface impl ---------- */
static esp_err_t fn_init(void *self)
{
    sensor_tsys01_t *s = (sensor_tsys01_t*)self;

#if CONFIG_TSYS01_SIMULATION
    s->initialized = true;
    return ESP_OK;
#else
    ESP_RETURN_ON_ERROR(ts_cmd(s, CMD_RESET), TAG, "reset");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(ts_read_prom(s), TAG, "prom");
    s->initialized = true;
    return ESP_OK;
#endif
}

static esp_err_t fn_read(void *self, sensor_measure_t *out)
{
    sensor_tsys01_t *s = (sensor_tsys01_t*)self;
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    out->ts_us = esp_timer_get_time();

#if CONFIG_TSYS01_SIMULATION
    double tmin = (double)CONFIG_TSYS01_SIM_TEMP_MIN_C;
    double tmax = (double)CONFIG_TSYS01_SIM_TEMP_MAX_C;
    out->temperature_c = rand_in(tmin, tmax);
    // TSYS01 mesure uniquement la température ; les autres champs peuvent rester à 0
    return ESP_OK;
#else
    if (!s->initialized) {
        esp_err_t e = fn_init(self);
        if (e != ESP_OK) return e;
    }

    uint32_t D = 0;
    ESP_RETURN_ON_ERROR(ts_read_temp_raw(s, &D), TAG, "adc");

    /* Polynôme d'interpolation (repris de ton code Arduino):
       T(°C) = -2*C1*1e-21*D^4 + 4*C2*1e-16*D^3 -2*C3*1e-11*D^2
               + 1*C4*1e-6*D  -1.5*C5*1e-2
       où C1..C5 = C[1]..C[5]
    */
    double t =
      (-2.0) * (double)s->C[1] * 1e-21 * pow((double)D, 4.0) +
      ( 4.0) * (double)s->C[2] * 1e-16 * pow((double)D, 3.0) +
      (-2.0) * (double)s->C[3] * 1e-11 * pow((double)D, 2.0) +
      ( 1.0) * (double)s->C[4] * 1e-6  * (double)D +
      (-1.5) * (double)s->C[5] * 1e-2;

    out->temperature_c = t;
    return ESP_OK;
#endif
}

static esp_err_t fn_sleep(void *self)
{
    (void)self;
    return ESP_OK;
}

static const char* fn_name(void *self)
{
    (void)self;
    return "TSYS01";
}

void sensor_tsys01_make(i2c_bus_t *bus, uint8_t addr, sensor_if_t *out)
{
    static sensor_tsys01_t inst; // simple singleton ; passe en alloc dynamique si tu veux plusieurs capteurs
    memset(&inst, 0, sizeof(inst));
    inst.bus  = bus;
    inst.addr = addr ? addr : CONFIG_TSYS01_I2C_ADDR;

    out->init  = fn_init;
    out->read  = fn_read;
    out->sleep = fn_sleep;
    out->name  = fn_name;
    out->self  = &inst;
}

