#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>   // size_t
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char id[32];          // identifiant unique plongée
    char date[20];        // ISO8601 "YYYY-MM-DDTHH:MM:SS"
    char location[64];    // ex: "Brest, France"
    char diver[32];       // nom du plongeur
} dive_metadata_t;

typedef struct {
    uint64_t timestamp;   // us depuis epoch
    float temperature;    // °C
    float pressure;       // bar
} dive_sample_t;

/** Initialise le FS (SPIFFS) et le répertoire /dives */
esp_err_t dive_storage_init(void);

/** Crée un nouveau dossier pour une plongée + fichier metadata */
esp_err_t dive_storage_create_dive(const dive_metadata_t *meta);

/** Ajoute un échantillon à une plongée existante */
esp_err_t dive_storage_append_sample(const char *dive_id, const dive_sample_t *sample);

/** Ferme la plongée (optionnel, ici juste flush) */
esp_err_t dive_storage_close_dive(const char *dive_id);

/** Liste les plongées enregistrées */
esp_err_t dive_storage_list(char ids[][32], size_t max, size_t *count);

/** Lit les métadonnées d’une plongée */
esp_err_t dive_storage_read_metadata(const char *dive_id, dive_metadata_t *meta);

/** Supprime une plongée */
esp_err_t dive_storage_delete(const char *dive_id);


/** Exporte une plongée en JSON (alloue une chaîne à free()).
 *  Format:
 *  {
 *    "id":"dive001","date":"...","location":"...","diver":"...",
 *    "samples":[{"ts_us":123,"temp_c":20.1,"press_bar":2.05}, ...]
 *  }
 */
esp_err_t dive_storage_export_dive_json(const char *dive_id, char **out_json, size_t *out_len);

/** Exporte toutes les plongées en JSON (tableau d’objets).
 *  Format: [ {<dive1>}, {<dive2>} , ... ]
 */
esp_err_t dive_storage_export_all_json(char **out_json, size_t *out_len);

#ifdef __cplusplus
}
#endif
