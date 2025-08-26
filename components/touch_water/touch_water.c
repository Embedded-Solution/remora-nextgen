#include "touch_water.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sleep.h"
#include <inttypes.h>

/* ==== Détection auto de l'API tactile disponible ==== */
#if __has_include("driver/touch_sens.h")
  #define TW_USE_NEW_API 1
  #include "driver/touch_sens.h"      // (IDF récents)
#else
  #define TW_USE_NEW_API 0
  #include "driver/touch_sensor.h"    // (legacy, IDF 4.x/5.x)
#endif

static const char *TAG = "touch_water";

static uint32_t s_baseline = 0;
static uint32_t s_threshold = 0;
static inline touch_pad_t tp(void) { return (touch_pad_t)CONFIG_APP_TOUCH_PAD_NUM; }

#ifndef CONFIG_APP_TOUCH_THRESH_PCT
#define CONFIG_APP_TOUCH_THRESH_PCT 70
#endif

#if TW_USE_NEW_API
/* =================== Nouvelle API =================== */
static inline esp_err_t tw_read_raw(uint32_t *raw) {
    return touch_sensor_read_raw(tp(), raw);
}

esp_err_t touch_water_init(void)
{
    ESP_LOGI(TAG, "Init (NEW API) pad=%d, thresh=%d%%",
             CONFIG_APP_TOUCH_PAD_NUM, CONFIG_APP_TOUCH_THRESH_PCT);

    touch_sensor_config_t cfg = {
        .pad = tp(),
        .voltage = TOUCH_HVOLT_2V7,
        .threshold = 0,
    };
    ESP_RETURN_ON_ERROR(touch_sensor_config(&cfg), TAG, "config");

    // Calibrage simple
    uint64_t sum = 0;
    const int samples = 20;
    for (int i = 0; i < samples; ++i) {
        uint32_t raw = 0;
        ESP_RETURN_ON_ERROR(touch_sensor_read_raw(tp(), &raw), TAG, "read_raw");
        sum += raw;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    s_baseline  = (uint32_t)(sum / samples);
    s_threshold = (s_baseline * CONFIG_APP_TOUCH_THRESH_PCT) / 100;

    ESP_RETURN_ON_ERROR(touch_sensor_set_threshold(tp(), s_threshold), TAG, "set_thresh");

    ESP_LOGI(TAG, "baseline=%" PRIu32 ", threshold=%" PRIu32, s_baseline, s_threshold);
    return ESP_OK;
}

void touch_water_deinit(void) {
    touch_sensor_deinit(tp());
}

bool touch_water_is_present(void)
{
    uint32_t raw = 0;
    if (touch_sensor_read_raw(tp(), &raw) != ESP_OK) return false;
    return (raw < s_threshold);
}

esp_err_t touch_water_prepare_wakeup(void)
{
    if (!s_threshold) ESP_RETURN_ON_ERROR(touch_water_init(), TAG, "reinit");
    return touch_sensor_set_threshold(tp(), s_threshold);
}

#else
/* =================== API legacy =================== */
static inline esp_err_t tw_read_raw(uint32_t *raw) {
    return touch_pad_read_raw_data(tp(), raw);
}

esp_err_t touch_water_init(void)
{
    ESP_LOGI(TAG, "Init (LEGACY API) pad=%d, thresh=%d%%",
             CONFIG_APP_TOUCH_PAD_NUM, CONFIG_APP_TOUCH_THRESH_PCT);

    ESP_RETURN_ON_ERROR(touch_pad_init(), TAG, "touch_pad_init");
    ESP_RETURN_ON_ERROR(touch_pad_set_fsm_mode(TOUCH_FSM_MODE_TIMER), TAG, "set_fsm");
    ESP_RETURN_ON_ERROR(touch_pad_config(tp()), TAG, "pad_config");

    /* Filtre si activé dans la config */
    #if CONFIG_TOUCH_PAD_FILTER_ENABLE
      touch_pad_set_filter_period(10);
      touch_pad_filter_enable();
    #endif

    vTaskDelay(pdMS_TO_TICKS(50));

    uint64_t sum = 0;
    const int samples = 20;
    for (int i = 0; i < samples; ++i) {
        uint32_t raw = 0;
        ESP_RETURN_ON_ERROR(touch_pad_read_raw_data(tp(), &raw), TAG, "read_raw");
        sum += raw;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    s_baseline  = (uint32_t)(sum / samples);
    s_threshold = (s_baseline * CONFIG_APP_TOUCH_THRESH_PCT) / 100;

    ESP_RETURN_ON_ERROR(touch_pad_set_thresh(tp(), s_threshold), TAG, "set_thresh");

    ESP_LOGI(TAG, "baseline=%" PRIu32 ", threshold=%" PRIu32, s_baseline, s_threshold);
    return ESP_OK;
}

void touch_water_deinit(void) {
    touch_pad_deinit();
}

bool touch_water_is_present(void)
{
    uint32_t raw = 0;
    if (tw_read_raw(&raw) != ESP_OK) return false;
    return (raw < s_threshold);
}

esp_err_t touch_water_prepare_wakeup(void)
{
    if (!s_threshold) ESP_RETURN_ON_ERROR(touch_water_init(), TAG, "reinit");
    return touch_pad_set_thresh(tp(), s_threshold);
}
#endif

uint32_t touch_water_get_baseline(void)  { return s_baseline;  }
uint32_t touch_water_get_threshold(void) { return s_threshold; }
uint32_t touch_water_read_raw(void)
{
    uint32_t raw = 0;
    tw_read_raw(&raw);
    return raw;
}
