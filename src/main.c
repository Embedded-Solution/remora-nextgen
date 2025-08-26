#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "driver/gpio.h"

#include "touch_water.h"
#include "wifi_net.h"

static const char *TAG = "main";

/* ===== HTTP simple ===== */
static esp_err_t http_post_json(const char *url, const char *json)
{
    esp_http_client_config_t cfg = { .url = url, .timeout_ms = 8000 };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) return ESP_FAIL;

    esp_http_client_set_method(cli, HTTP_METHOD_POST);
    esp_http_client_set_header(cli, "Content-Type", "application/json");
    esp_http_client_set_post_field(cli, json, strlen(json));
    esp_err_t err = esp_http_client_perform(cli);
    if (err == ESP_OK) {
        int st = esp_http_client_get_status_code(cli);
        ESP_LOGI(TAG, "HTTP status=%d", st);
        err = (st >= 200 && st < 300) ? ESP_OK : ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "HTTP error: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(cli);
    return err;
}

/* ===== Tasks ===== */
static void uploadDives_task(void *arg)
{
    ESP_LOGI(TAG, "uploadDives start");
    if (wifi_net_connect(10000) == ESP_OK) {
        const char *payload = "{\"device\":\"esp32-s3\",\"action\":\"upload_dives\"}";
        (void)http_post_json(CONFIG_APP_UPLOAD_URL, payload);
    }
    wifi_net_stop();   // libère la radio
    ESP_LOGI(TAG, "uploadDives done");
    vTaskDelete(NULL);
}

static void newDive_task(void *arg)
{
    ESP_LOGI(TAG, "newDive start (baseline=%" PRIu32 ", thr=%" PRIu32 ")",
             touch_water_get_baseline(), touch_water_get_threshold());

    for (int i = 0; i < 10; ++i) {
        bool wet = touch_water_is_present();
        uint32_t raw = touch_water_read_raw();
        ESP_LOGI(TAG, "raw=%" PRIu32 " water=%s", raw, wet ? "YES" : "no");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGI(TAG, "newDive done");
    vTaskDelete(NULL);
}

/* ===== Deep sleep helpers ===== */
static void configure_wake_sources(void)
{
    gpio_pulldown_en(CONFIG_APP_VBUS_SENSE_GPIO);
    gpio_pullup_dis(CONFIG_APP_VBUS_SENSE_GPIO);
    esp_sleep_enable_ext1_wakeup(1ULL << CONFIG_APP_VBUS_SENSE_GPIO, ESP_EXT1_WAKEUP_ANY_HIGH);

    touch_water_prepare_wakeup();
    esp_sleep_enable_touchpad_wakeup();
}

static void go_to_deep_sleep(void)
{
    ESP_LOGI(TAG, "Entering deep sleep...");
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_deep_sleep_start();
}

void app_main(void)
{
    // NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase(); nvs_flash_init();
    }

    // Tactile
    touch_water_init();

    // Wi-Fi (pile prête, mais pas connecté tant qu'on ne l'appelle pas)
    wifi_net_init();

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    ESP_LOGI(TAG, "Wake cause=%d (0=cold, 3=touch, 6=ext1)", cause);

    bool launched = false;

    if (cause == ESP_SLEEP_WAKEUP_TOUCHPAD) {
        xTaskCreate(newDive_task, "newDive", 4096, NULL, 5, NULL);
        launched = true;
    } else if (cause == ESP_SLEEP_WAKEUP_EXT1) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << CONFIG_APP_VBUS_SENSE_GPIO,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_ENABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&io);
        if (gpio_get_level(CONFIG_APP_VBUS_SENSE_GPIO) == 1) {
            xTaskCreate(uploadDives_task, "uploadDives", 8192, NULL, 5, NULL);
            launched = true;
        }
    }

    const int64_t t0 = esp_timer_get_time();
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(200));
        int64_t elapsed_s = (esp_timer_get_time() - t0) / 1000000;
        if (!launched) {
            ESP_LOGI(TAG, "No task -> sleep");
            break;
        }
        if (elapsed_s >= CONFIG_APP_MAX_RUN_SECONDS) {
            ESP_LOGW(TAG, "Timeout (%ds) -> sleep", CONFIG_APP_MAX_RUN_SECONDS);
            break;
        }
    }

    configure_wake_sources();

    // Stop & deinit propre (optionnel)
    wifi_net_stop();
    // wifi_net_deinit(); // si tu veux aussi libérer le driver

    go_to_deep_sleep();
}
