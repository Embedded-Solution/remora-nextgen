#include "app_dive.h"
#include "touch_water.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <inttypes.h>

static const char *TAG = "app_dive";

static void dive_task(void *arg)
{
    ESP_LOGI(TAG, "newDive start (baseline=%" PRIu32 ", thr=%" PRIu32 ")",
             touch_water_get_baseline(), touch_water_get_threshold());
    for (int i=0; i<10; ++i) {
        bool wet = touch_water_is_present();
        uint32_t raw = touch_water_read_raw();
        ESP_LOGI(TAG, "raw=%" PRIu32 " water=%s", raw, wet ? "YES" : "no");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_LOGI(TAG, "newDive done");
    vTaskDelete(NULL);
}

esp_err_t app_dive_start(void)
{
    if (xTaskCreate(dive_task, "dive", 4096, NULL, 5, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;
    return ESP_OK;
}
