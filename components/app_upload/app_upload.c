#include "app_upload.h"
#include "wifi_net.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "app_upload";

#ifndef CONFIG_APP_UPLOAD_URL
#define CONFIG_APP_UPLOAD_URL "http://example.com/api/dives/upload"
#endif

static void upload_task(void *arg)
{
    ESP_LOGI(TAG, "upload start");
    if (wifi_net_connect(10000) == ESP_OK)
    {
        const char *json = "{\"device\":\"esp32-s3\",\"action\":\"upload_dives\"}";
        esp_http_client_config_t cfg = {.url = CONFIG_APP_UPLOAD_URL, .timeout_ms = 8000};
        esp_http_client_handle_t cli = esp_http_client_init(&cfg);
        if (cli)
        {
            esp_http_client_set_method(cli, HTTP_METHOD_POST);
            esp_http_client_set_header(cli, "Content-Type", "application/json");
            esp_http_client_set_post_field(cli, json, strlen(json));
            esp_err_t err = esp_http_client_perform(cli);
            int st = esp_http_client_get_status_code(cli);
            ESP_LOGI(TAG, "HTTP: err=%s status=%d", esp_err_to_name(err), st);
            esp_http_client_cleanup(cli);
        }
    }
    wifi_net_stop();
    ESP_LOGI(TAG, "upload done");
    vTaskDelete(NULL);
}

esp_err_t app_upload_start(void)
{
    if (xTaskCreate(upload_task, "upload", 8192, NULL, 5, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;
    return ESP_OK;
}
