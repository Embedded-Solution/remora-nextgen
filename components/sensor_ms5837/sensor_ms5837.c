#include "sensor_ms5837.h"
#include "esp_log.h"
#include "esp_check.h" 
#include "esp_timer.h"
#include "esp_random.h" 
#include <string.h>

static const char *TAG = "MS5837";

#define CMD_RESET   0x1E
#define CMD_ADC_READ 0x00
#define CMD_D1_OSR_8192 0x4A
#define CMD_D2_OSR_8192 0x5A
#define CMD_PROM_READ 0xA0

#ifndef CONFIG_MS5837_I2C_ADDR
#define CONFIG_MS5837_I2C_ADDR 0x76
#endif

/* ---------- Simulation helpers ---------- */
#if CONFIG_MS5837_SIMULATION
static uint32_t urand32(void) {
    return esp_random();
}
static double rand_in(double min, double max) {
    uint32_t r = urand32();
    double f = (double)r / (double)UINT32_MAX;
    return min + f * (max - min);
}
#endif

/* ---------- Low-level I2C helpers ---------- */
static esp_err_t ms_cmd(sensor_ms5837_t *s, uint8_t cmd)
{
    return i2c_bus_write(s->bus, s->addr, &cmd, 1, pdMS_TO_TICKS(20));
}
static esp_err_t ms_read24(sensor_ms5837_t *s, uint32_t *out)
{
    uint8_t b[3] = {0};
    esp_err_t e = i2c_bus_write_read(s->bus, s->addr, (uint8_t[]){CMD_ADC_READ}, 1, b, 3, pdMS_TO_TICKS(20));
    if (e != ESP_OK) return e;
    *out = ((uint32_t)b[0] << 16) | ((uint32_t)b[1] << 8) | b[2];
    return ESP_OK;
}
static esp_err_t ms_read_prom(sensor_ms5837_t *s)
{
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

/* ---------- Computation (datasheet) ---------- */
static esp_err_t ms_read_adc(sensor_ms5837_t *s)
{
    ESP_RETURN_ON_ERROR(ms_cmd(s, CMD_D1_OSR_8192), TAG, "D1 cmd");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(ms_read24(s, &s->D1_raw), TAG, "D1 read");

    ESP_RETURN_ON_ERROR(ms_cmd(s, CMD_D2_OSR_8192), TAG, "D2 cmd");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(ms_read24(s, &s->D2_raw), TAG, "D2 read");
    return ESP_OK;
}

static void ms_compute(sensor_ms5837_t *s)
{
    // cf. datasheet MS5837-30BA (formules 64-bit)
    s->dT   = (int32_t)s->D2_raw - ((int32_t)s->C[5] << 8);
    s->TEMP = 2000 + ((int64_t)s->dT * (int64_t)s->C[6] >> 23); // centi-degC
    s->OFF  = ((int64_t)s->C[2] << 16) + (((int64_t)s->C[4] * (int64_t)s->dT) >> 7);
    s->SENS = ((int64_t)s->C[1] << 15) + (((int64_t)s->C[3] * (int64_t)s->dT) >> 8);

    // compensation 2e ordre (simplifiée autour de 20°C)
    if (s->TEMP < 2000) {
        int32_t T2 = ((int64_t)s->dT * s->dT) >> 31;
        int64_t OFF2 = 5 * (int64_t)(s->TEMP - 2000) * (s->TEMP - 2000) >> 1;
        int64_t SENS2 = 5 * (int64_t)(s->TEMP - 2000) * (s->TEMP - 2000) >> 2;
        s->TEMP -= T2;
        s->OFF  -= OFF2;
        s->SENS -= SENS2;
    }
}

/* ---------- sensor_if_t implementation ---------- */
static esp_err_t fn_init(void *self)
{
    sensor_ms5837_t *s = (sensor_ms5837_t*)self;

#if CONFIG_MS5837_SIMULATION
    s->initialized = true;
    return ESP_OK;
#else
    // Reset
    ESP_RETURN_ON_ERROR(ms_cmd(s, CMD_RESET), TAG, "reset");
    vTaskDelay(pdMS_TO_TICKS(10));
    // PROM
    ESP_RETURN_ON_ERROR(ms_read_prom(s), TAG, "prom");
    s->initialized = true;
    return ESP_OK;
#endif
}

static esp_err_t fn_read(void *self, sensor_measure_t *out)
{
    sensor_ms5837_t *s = (sensor_ms5837_t*)self;
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    out->ts_us = esp_timer_get_time();

#if CONFIG_MS5837_SIMULATION
    // Temp en °C
    double tmin = (double)CONFIG_MS5837_SIM_TEMP_MIN_C;
    double tmax = (double)CONFIG_MS5837_SIM_TEMP_MAX_C;
    if (tmax < tmin) { double tmp = tmin; tmin = tmax; tmax = tmp; }
    out->temperature_c = rand_in(tmin, tmax);

    // Pression en bar: bornes en mbar dans Kconfig -> converties en bar
    double pmin_bar = ((double)CONFIG_MS5837_SIM_PRESS_MIN_BAR) / 1000.0;
    double pmax_bar = ((double)CONFIG_MS5837_SIM_PRESS_MAX_BAR) / 1000.0;
    if (pmax_bar < pmin_bar) { double tmp = pmin_bar; pmin_bar = pmax_bar; pmax_bar = tmp; }
    out->pressure_bar = rand_in(pmin_bar, pmax_bar);

    // Profondeur (approx eau de mer) à partir de la pression absolue
    const double p0_bar = 1.013; // pression atmosphérique env.
    const double rho = 1029.0;   // kg/m3
    const double g = 9.80665;
    double dp_pa = (out->pressure_bar - p0_bar) * 1e5;
    out->depth_m = (dp_pa > 0) ? dp_pa / (rho * g) : 0.0;

    return ESP_OK;
#else
    if (!s->initialized) {
        esp_err_t e = fn_init(self);
        if (e != ESP_OK) return e;
    }

    ESP_RETURN_ON_ERROR(ms_read_adc(s), TAG, "adc");
    ms_compute(s);

    // pression en Pa: P = ((D1*SENS/2^21 - OFF)/2^13)
    int64_t P = (((int64_t)s->D1_raw * (s->SENS >> 21) - s->OFF) >> 13);
    double pressure_pa  = (double)P;              // Pa
    double pressure_bar = pressure_pa / 1e5;      // bar
    double temp_c       = ((double)s->TEMP) / 100.0;

    out->temperature_c = temp_c;
    out->pressure_bar  = pressure_bar;

    const double p0_bar = 1.013;
    const double rho = 1029.0, g = 9.80665;
    double dp_pa = (pressure_bar - p0_bar) * 1e5;
    out->depth_m = (dp_pa > 0) ? dp_pa / (rho * g) : 0.0;

    return ESP_OK;
#endif
}

static esp_err_t fn_sleep(void *self)
{
    (void)self;
    // MS5837 pas de "sleep" formel -> no-op
    return ESP_OK;
}

static const char* fn_name(void *self)
{
    (void)self;
    return "MS5837";
}

void sensor_ms5837_make(i2c_bus_t *bus, uint8_t addr, sensor_if_t *out)
{
    static sensor_ms5837_t inst; // squelette simple ; pour plus d’instances, allouer dynamiquement
    memset(&inst, 0, sizeof(inst));
    inst.bus = bus;
    inst.addr = addr ? addr : CONFIG_MS5837_I2C_ADDR;

    out->init  = fn_init;
    out->read  = fn_read;
    out->sleep = fn_sleep;
    out->name  = fn_name;
    out->self  = &inst;
}
