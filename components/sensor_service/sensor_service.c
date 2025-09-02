#include "sensor_service.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>

#define TAG "sensor_service"

typedef struct {
    sensor_if_t sensor;
    TickType_t  period;
    TickType_t  next_due;
    char        name[16];
    uint8_t     err_streak;
} slot_t;

struct sensor_service {
    i2c_bus_t*     bus;
    QueueHandle_t  q;
    TaskHandle_t   task;
    slot_t*        slots;
    size_t         cap;
    size_t         n;
    bool           running;
};

static void poll_task(void* arg)
{
    sensor_service_t* s = (sensor_service_t*)arg;

    // init lazy des capteurs
    for (size_t i=0;i<s->n;i++) {
        if (s->slots[i].sensor.init) {
            esp_err_t e = s->slots[i].sensor.init(s->slots[i].sensor.self);
            if (e != ESP_OK) {
                ESP_LOGW(TAG, "[%s] init failed: %s", s->slots[i].name, esp_err_to_name(e));
            }
        }
        s->slots[i].next_due = xTaskGetTickCount(); // due asap
    }

    while (s->running) {
        // Cherche l’échéance la plus proche
        TickType_t now = xTaskGetTickCount();
        TickType_t next = now + pdMS_TO_TICKS(1000); // fallback 1s
        for (size_t i=0;i<s->n;i++) {
            if ((int32_t)(s->slots[i].next_due - now) <= 0) {
                // Poll
                sensor_measure_t m;
                esp_err_t e = s->slots[i].sensor.read(s->slots[i].sensor.self, &m);
                if (e == ESP_OK) {
                    s->slots[i].err_streak = 0;
                    sensor_sample_msg_t msg = {0};
                    strncpy(msg.name, s->slots[i].name, sizeof(msg.name)-1);
                    msg.measure = m;
                    (void)xQueueSend(s->q, &msg, 0);
                } else {
                    if (s->slots[i].err_streak < 200) s->slots[i].err_streak++;
                    ESP_LOGW(TAG, "[%s] read err(%u): %s",
                             s->slots[i].name, s->slots[i].err_streak, esp_err_to_name(e));
                    // petit backoff si erreurs répétées
                    TickType_t backoff = pdMS_TO_TICKS(50 * (s->slots[i].err_streak > 10 ? 10 : s->slots[i].err_streak));
                    s->slots[i].next_due = now + backoff;
                    continue;
                }
                s->slots[i].next_due = now + s->slots[i].period;
            }
            if (s->slots[i].next_due < next) next = s->slots[i].next_due;
        }

        TickType_t delay = (next > now) ? (next - now) : 1;
        vTaskDelay(delay);
    }

    // sleep() friendly (optionnel)
    for (size_t i=0;i<s->n;i++) {
        if (s->slots[i].sensor.sleep) (void)s->slots[i].sensor.sleep(s->slots[i].sensor.self);
    }
    vTaskDelete(NULL);
}

sensor_service_t* sensor_service_create(i2c_bus_t* bus,
                                        size_t max_sensors,
                                        size_t queue_len)
{
    if (!bus || max_sensors == 0 || queue_len == 0) return NULL;

    sensor_service_t* s = calloc(1, sizeof(*s));
    if (!s) return NULL;

    s->bus = bus;
    s->slots = calloc(max_sensors, sizeof(slot_t));
    if (!s->slots) { free(s); return NULL; }

    s->cap = max_sensors;
    s->q = xQueueCreate(queue_len, sizeof(sensor_sample_msg_t));
    if (!s->q) { free(s->slots); free(s); return NULL; }

    return s;
}

esp_err_t sensor_service_add(sensor_service_t* svc,
                             sensor_if_t sensor,
                             uint32_t period_ms,
                             const char* short_name)
{
    if (!svc || svc->n >= svc->cap || period_ms == 0 || !short_name) return ESP_ERR_INVALID_ARG;
    slot_t* slot = &svc->slots[svc->n++];
    memset(slot, 0, sizeof(*slot));
    slot->sensor = sensor;
    slot->period = pdMS_TO_TICKS(period_ms);
    strncpy(slot->name, short_name, sizeof(slot->name)-1);
    slot->next_due = xTaskGetTickCount();
    return ESP_OK;
}

esp_err_t sensor_service_start(sensor_service_t* svc, UBaseType_t prio, uint32_t stack_words)
{
    if (!svc || svc->running) return ESP_ERR_INVALID_STATE;
    svc->running = true;
    if (xTaskCreate(poll_task, "sensor_poll", stack_words ? stack_words : 4096, svc,
                    prio ? prio : 5, &svc->task) != pdPASS) {
        svc->running = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void sensor_service_destroy(sensor_service_t* svc)
{
    if (!svc) return;
    svc->running = false;
    // La tâche se termine toute seule; si tu veux forcer, attends un peu:
    vTaskDelay(pdMS_TO_TICKS(20));
    if (svc->q) vQueueDelete(svc->q);
    if (svc->slots) free(svc->slots);
    free(svc);
}

QueueHandle_t sensor_service_get_queue(sensor_service_t* svc)
{
    return svc ? svc->q : NULL;
}
