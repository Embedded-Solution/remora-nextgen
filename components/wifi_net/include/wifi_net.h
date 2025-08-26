#pragma once
#include <stdbool.h>
#include "sdkconfig.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialise netif, event loop, driver Wi-Fi (mode STA). */
esp_err_t wifi_net_init(void);

/** Démarre le Wi-Fi STA et tente la connexion.
 * @param timeout_ms  Delai max (ex: 10000). 0 = non bloquant.
 * @return ESP_OK si connecté, ESP_ERR_TIMEOUT si délai, autre code sinon.
 */
esp_err_t wifi_net_connect(int timeout_ms);

/** True si la station est connectée (IP obtenue). */
bool wifi_net_is_connected(void);

/** Arrête le Wi-Fi (esp_wifi_stop), garde la pile initialisée. */
esp_err_t wifi_net_stop(void);

/** Désinitialise le Wi-Fi (unregister handlers, esp_wifi_deinit). */
esp_err_t wifi_net_deinit(void);

#ifdef __cplusplus
}
#endif
