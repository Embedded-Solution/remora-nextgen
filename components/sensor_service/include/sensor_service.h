#pragma once
#include "sensor.h"
#include "i2c_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sensor_service sensor_service_t;

/* Message publié par le service pour chaque lecture réussie */
typedef struct {
    char              name[16];       // nom court du capteur
    sensor_measure_t  measure;        // structure normalisée (temp, press, depth, ts)
} sensor_sample_msg_t;

/** Crée le service de polling (ne démarre pas la tâche) */
sensor_service_t* sensor_service_create(i2c_bus_t* bus,
                                        size_t max_sensors,
                                        size_t queue_len);

/** Ajoute un capteur avec sa période (ms) */
esp_err_t sensor_service_add(sensor_service_t* svc,
                             sensor_if_t sensor,
                             uint32_t period_ms,
                             const char* short_name);   // ex "TSYS", "MS5837"

/** Démarre la tâche FreeRTOS de polling */
esp_err_t sensor_service_start(sensor_service_t* svc, UBaseType_t prio, uint32_t stack_words);

/** Arrête la tâche et libère la ressource */
void sensor_service_destroy(sensor_service_t* svc);

/** Récupère la queue (à consommer dans une autre tâche) */
QueueHandle_t sensor_service_get_queue(sensor_service_t* svc);

#ifdef __cplusplus
}
#endif
