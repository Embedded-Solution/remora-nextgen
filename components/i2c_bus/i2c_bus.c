#include "i2c_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_check.h" 

struct i2c_bus {
    i2c_port_t port;
    SemaphoreHandle_t mtx;
};

esp_err_t i2c_bus_create(i2c_port_t port, gpio_num_t sda, gpio_num_t scl, uint32_t hz, i2c_bus_t **out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = NULL;

    i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = hz,
    };
    ESP_RETURN_ON_ERROR(i2c_param_config(port, &cfg), "i2c", "param_config");
    ESP_RETURN_ON_ERROR(i2c_driver_install(port, I2C_MODE_MASTER, 0, 0, 0), "i2c", "install");

    i2c_bus_t *b = calloc(1, sizeof(*b));
    if (!b) return ESP_ERR_NO_MEM;
    b->port = port;
    b->mtx = xSemaphoreCreateMutex();
    if (!b->mtx) { i2c_driver_delete(port); free(b); return ESP_ERR_NO_MEM; }

    *out = b;
    return ESP_OK;
}

void i2c_bus_destroy(i2c_bus_t *bus)
{
    if (!bus) return;
    vSemaphoreDelete(bus->mtx);
    i2c_driver_delete(bus->port);
    free(bus);
}

esp_err_t i2c_bus_write_read(i2c_bus_t *bus, uint8_t addr,
                             const uint8_t *w, size_t wl,
                             uint8_t *r, size_t rl,
                             TickType_t timeout_ticks)
{
    if (!bus) return ESP_ERR_INVALID_ARG;

    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < 3; ++attempt) {
        xSemaphoreTake(bus->mtx, portMAX_DELAY);

        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        if (wl && w) {
            i2c_master_start(cmd);
            i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
            i2c_master_write(cmd, (uint8_t*)w, wl, true);
        }
        if (rl && r) {
            i2c_master_start(cmd);
            i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);
            if (rl > 1) {
                i2c_master_read(cmd, r, rl - 1, I2C_MASTER_ACK);
            }
            i2c_master_read_byte(cmd, r + rl - 1, I2C_MASTER_NACK);
        }
        i2c_master_stop(cmd);
        err = i2c_master_cmd_begin(bus->port, cmd, timeout_ticks);
        i2c_cmd_link_delete(cmd);

        xSemaphoreGive(bus->mtx);

        if (err == ESP_OK) break;
        vTaskDelay(pdMS_TO_TICKS(5 * (1 << attempt))); // petit backoff
    }
    return err;
}
