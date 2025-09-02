#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "driver/gpio.h"

#include "touch_water.h"
#include "wifi_net.h"
#include "app_upload.h"
#include "app_dive.h"
#include "i2c_bus.h"
#include "sensor.h"
#include "sensor_tsys01.h"
#include "sensor_ms5837.h"
#include "sensor_service.h"

// Filets de sécurité
#ifndef CONFIG_APP_MAX_RUN_SECONDS
#define CONFIG_APP_MAX_RUN_SECONDS 120
#endif
#ifndef CONFIG_APP_VBUS_SENSE_GPIO
#define CONFIG_APP_VBUS_SENSE_GPIO 4
#endif
#ifndef CONFIG_APP_DEBUG_DISABLE_VBUS_WAKE
#define CONFIG_APP_DEBUG_DISABLE_VBUS_WAKE 0
#endif

static const char *TAG = "main";




/* Adapte SDA/SCL selon ta carte */
#ifndef I2C_SDA_GPIO
#define I2C_SDA_GPIO GPIO_NUM_8
#endif
#ifndef I2C_SCL_GPIO
#define I2C_SCL_GPIO GPIO_NUM_9
#endif

static void consumer_task(void* arg)
{
    QueueHandle_t q = (QueueHandle_t)arg;
    sensor_sample_msg_t msg;
    while (1) {
        if (xQueueReceive(q, &msg, portMAX_DELAY)) {
            const sensor_measure_t* m = &msg.measure;
            ESP_LOGI("samples", "[%s] T=%.2f C, P=%.3f bar, depth=%.2f m (ts=%llu us)",
                     msg.name, m->temperature_c, m->pressure_bar, m->depth_m,
                     (unsigned long long)m->ts_us);
        }
    }
}


static void configure_wake_sources(void)
{
#if !CONFIG_APP_DEBUG_DISABLE_VBUS_WAKE
    gpio_pulldown_en(CONFIG_APP_VBUS_SENSE_GPIO);
    gpio_pullup_dis(CONFIG_APP_VBUS_SENSE_GPIO);
    esp_sleep_enable_ext1_wakeup(1ULL << CONFIG_APP_VBUS_SENSE_GPIO, ESP_EXT1_WAKEUP_ANY_HIGH);
#else
    ESP_LOGW(TAG, "DEBUG: EXT1 (VBUS) wake DISABLED");
#endif

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
#if CONFIG_APP_DEBUG_DISABLE_VBUS_WAKE
    ESP_LOGW(TAG, "Config: EXT1 (VBUS) DISABLED by CONFIG");
#else
    ESP_LOGI(TAG, "Config: EXT1 (VBUS) ENABLED");
#endif

/*
    // NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // Init capteur tactile
    ESP_ERROR_CHECK(touch_water_init());

    // Pile Wi-Fi prête (ne se connecte que si app_upload_start() est appelé)
    wifi_net_init();
*/

//I2C
    // 1) Bus I²C commun
    i2c_bus_t *bus = NULL;
    ESP_ERROR_CHECK(i2c_bus_create(I2C_NUM_0, I2C_SDA_GPIO, I2C_SCL_GPIO, 400000, &bus));

    // 2) Instancier les capteurs
    sensor_if_t tsys;
    sensor_tsys01_make(bus, 0x77, &tsys);    // TSYS01 (temp)
    sensor_if_t ms;
    sensor_ms5837_make(bus, 0x76, &ms);      // MS5837 (temp+pression+depth)

    // 3) Créer le service de polling
    sensor_service_t* svc = sensor_service_create(bus, /*max_sensors*/ 4, /*queue_len*/ 16);
    assert(svc);

    // 4) Enregistrer les capteurs avec leur période
    ESP_ERROR_CHECK(sensor_service_add(svc, tsys, /*period_ms*/ 1000, "TSYS"));
    ESP_ERROR_CHECK(sensor_service_add(svc, ms,   /*period_ms*/  500, "MS5837"));

    // 5) Démarrer la tâche de polling
    ESP_ERROR_CHECK(sensor_service_start(svc, /*prio*/5, /*stack_words*/4096));

    // 6) Tâche consommatrice (lecture de la queue)
    QueueHandle_t q = sensor_service_get_queue(svc);
    xTaskCreate(consumer_task, "samples_consumer", 4096, (void*)q, 5, NULL);

    // … à partir d’ici, tu peux router ces échantillons :
    //    - vers `dive_storage` (CSV/JSON),
    //    - vers un `http_post_json` (upload),
    //    - vers un contrôle (seuils, LED, …).


    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    ESP_LOGI(TAG, "Wake cause=%d (0=cold, 3=touch, 6=ext1)", cause);

    bool launched = false;

    if (cause == ESP_SLEEP_WAKEUP_TOUCHPAD)
    {
        app_dive_start();
        launched = true;
    }
#if !CONFIG_APP_DEBUG_DISABLE_VBUS_WAKE
    else if (cause == ESP_SLEEP_WAKEUP_EXT1)
    {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << CONFIG_APP_VBUS_SENSE_GPIO,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_ENABLE,
            .intr_type = GPIO_INTR_DISABLE};
        gpio_config(&io);
        int vbus = gpio_get_level(CONFIG_APP_VBUS_SENSE_GPIO);
        ESP_LOGI(TAG, "EXT1 wake, VBUS=%d", vbus);
        if (vbus == 1)
        {
            app_upload_start();
            launched = true;
        }
    }
#endif
    else
    {
        // Cold boot : on teste les triggers
        ESP_LOGI(TAG, "Cold boot -> probing triggers before sleep");

#if !CONFIG_APP_DEBUG_DISABLE_VBUS_WAKE
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << CONFIG_APP_VBUS_SENSE_GPIO,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_ENABLE,
            .intr_type = GPIO_INTR_DISABLE};
        gpio_config(&io);
        int vbus = gpio_get_level(CONFIG_APP_VBUS_SENSE_GPIO);
        if (vbus == 1)
        {
            ESP_LOGI(TAG, "VBUS present at cold boot -> upload");
            app_upload_start();
            launched = true;
        }
        else
#endif
        {
            bool wet = touch_water_is_present();
            ESP_LOGI(TAG, "Water at boot: %s", wet ? "YES" : "no");
            if (wet)
            {
                app_dive_start();
                launched = true;
            }
        }
    }

    // Petite boucle garde-fou puis sommeil
    const int64_t t0 = esp_timer_get_time();
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(200));
        int64_t elapsed_s = (esp_timer_get_time() - t0) / 1000000;
        if (!launched)
        {
            ESP_LOGI(TAG, "No task -> sleep");
            break;
        }
        if (elapsed_s >= CONFIG_APP_MAX_RUN_SECONDS)
        {
            ESP_LOGW(TAG, "Timeout (%ds) -> sleep", CONFIG_APP_MAX_RUN_SECONDS);
            break;
        }
    }

    configure_wake_sources();
    wifi_net_stop(); // coupe la radio si elle a été utilisée
    go_to_deep_sleep();
}
