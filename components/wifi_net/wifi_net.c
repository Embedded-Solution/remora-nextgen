#include "wifi_net.h"
#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "wifi_net";

static EventGroupHandle_t s_evt;
#define WIFI_CONNECTED_BIT BIT0

static bool s_inited = false;
static bool s_started = false;

static void handler(void* arg, esp_event_base_t base, int32_t id, void* data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        // On retente, tant que starté
        if (s_started) esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_evt, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_net_init(void)
{
    if (s_inited) return ESP_OK;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default()); // idempotent si déjà créé

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi_init");

    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &handler, NULL, NULL), TAG, "reg_wifi");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &handler, NULL, NULL), TAG, "reg_ip");

    s_evt = xEventGroupCreate();
    s_inited = true;
    return ESP_OK;
}

esp_err_t wifi_net_connect(int timeout_ms)
{
    ESP_RETURN_ON_ERROR(wifi_net_init(), TAG, "init_first");

    wifi_config_t sta = {0};
    strlcpy((char*)sta.sta.ssid,     CONFIG_APP_WIFI_SSID, sizeof(sta.sta.ssid));
    strlcpy((char*)sta.sta.password, CONFIG_APP_WIFI_PASS, sizeof(sta.sta.password));
    sta.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set_mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &sta), TAG, "set_cfg");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start");
    s_started = true;

    if (timeout_ms <= 0) return ESP_OK;

    EventBits_t bits = xEventGroupWaitBits(
        s_evt, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));

    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGE(TAG, "connect timeout (%d ms)", timeout_ms);
        return ESP_ERR_TIMEOUT;
    }
    ESP_LOGI(TAG, "connected to SSID='%s'", CONFIG_APP_WIFI_SSID);
    return ESP_OK;
}

bool wifi_net_is_connected(void)
{
    if (!s_evt) return false;
    EventBits_t bits = xEventGroupGetBits(s_evt);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

esp_err_t wifi_net_stop(void)
{
    if (!s_inited || !s_started) return ESP_OK;
    s_started = false;
    xEventGroupClearBits(s_evt, WIFI_CONNECTED_BIT);
    return esp_wifi_stop();
}

esp_err_t wifi_net_deinit(void)
{
    if (!s_inited) return ESP_OK;

    wifi_net_stop();

    esp_event_handler_instance_unregister(IP_EVENT,   IP_EVENT_STA_GOT_IP,  &handler);
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,     &handler);

    ESP_ERROR_CHECK(esp_wifi_deinit());
    if (s_evt) { vEventGroupDelete(s_evt); s_evt = NULL; }

    s_inited = false;
    return ESP_OK;
}
