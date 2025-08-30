#include "dive_storage.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include "cJSON.h"

static const char *TAG = "dive_storage";

esp_err_t dive_storage_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 10,
        .format_if_mount_failed = true};
    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&conf));

    // Vérifie ou crée le répertoire
    struct stat st;
    if (stat("/spiffs/dives", &st) != 0)
    {
        ESP_LOGI(TAG, "Creating /spiffs/dives");
        mkdir("/spiffs/dives", 0777);
    }
    return ESP_OK;
}

static void build_path(const char *dive_id, const char *fname, char *out, size_t out_sz)
{
    snprintf(out, out_sz, "/spiffs/dives/%s/%s", dive_id, fname);
}

esp_err_t dive_storage_create_dive(const dive_metadata_t *meta)
{
    char path[128];
    snprintf(path, sizeof(path), "/spiffs/dives/%s", meta->id);

    if (mkdir(path, 0777) != 0)
    {
        ESP_LOGE(TAG, "mkdir %s failed", path);
        return ESP_FAIL;
    }

    char file[160];
    build_path(meta->id, "metadata.txt", file, sizeof(file));

    FILE *f = fopen(file, "w");
    if (!f)
        return ESP_FAIL;
    fprintf(f, "id=%s\n", meta->id);
    fprintf(f, "date=%s\n", meta->date);
    fprintf(f, "location=%s\n", meta->location);
    fprintf(f, "diver=%s\n", meta->diver);
    fclose(f);

    build_path(meta->id, "data.csv", file, sizeof(file));
    f = fopen(file, "w");
    if (!f)
        return ESP_FAIL;
    fprintf(f, "timestamp_us,temperature_C,pressure_bar\n");
    fclose(f);

    return ESP_OK;
}

esp_err_t dive_storage_append_sample(const char *dive_id, const dive_sample_t *sample)
{
    char file[160];
    build_path(dive_id, "data.csv", file, sizeof(file));
    FILE *f = fopen(file, "a");
    if (!f)
        return ESP_FAIL;
    fprintf(f, "%llu,%.2f,%.2f\n",
            (unsigned long long)sample->timestamp,
            sample->temperature,
            sample->pressure);
    fclose(f);
    return ESP_OK;
}

esp_err_t dive_storage_close_dive(const char *dive_id)
{
    // Rien à faire : fichiers sont flushés à chaque append
    ESP_LOGI(TAG, "Dive %s closed", dive_id);
    return ESP_OK;
}

esp_err_t dive_storage_list(char ids[][32], size_t max, size_t *count)
{
    DIR *dir = opendir("/spiffs/dives");
    if (!dir)
        return ESP_FAIL;

    struct dirent *ent;
    size_t n = 0;
    while ((ent = readdir(dir)) != NULL && n < max)
    {
        if (ent->d_type == DT_DIR && strcmp(ent->d_name, ".") && strcmp(ent->d_name, ".."))
        {
            strncpy(ids[n], ent->d_name, 31);
            ids[n][31] = 0;
            n++;
        }
    }
    closedir(dir);
    *count = n;
    return ESP_OK;
}

esp_err_t dive_storage_read_metadata(const char *dive_id, dive_metadata_t *meta)
{
    char file[160];
    build_path(dive_id, "metadata.txt", file, sizeof(file));

    FILE *f = fopen(file, "r");
    if (!f)
        return ESP_FAIL;

    char line[128];
    while (fgets(line, sizeof(line), f))
    {
        if (sscanf(line, "id=%31s", meta->id) == 1)
            continue;
        if (sscanf(line, "date=%19s", meta->date) == 1)
            continue;
        if (sscanf(line, "location=%63[^\n]", meta->location) == 1)
            continue;
        if (sscanf(line, "diver=%31s", meta->diver) == 1)
            continue;
    }
    fclose(f);
    return ESP_OK;
}

esp_err_t dive_storage_delete(const char *dive_id)
{
    char path[128];
    snprintf(path, sizeof(path), "/spiffs/dives/%s", dive_id);

    char file[160];
    build_path(dive_id, "metadata.txt", file, sizeof(file));
    unlink(file);
    build_path(dive_id, "data.csv", file, sizeof(file));
    unlink(file);
    rmdir(path);

    return ESP_OK;
}

// --- utilitaire : lit un fichier entier en mémoire (optionnel ici) ---
static char *read_text_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0)
    {
        fclose(f);
        return NULL;
    }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf)
    {
        fclose(f);
        return NULL;
    }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';
    if (out_len)
        *out_len = rd;
    return buf;
}

// --- construit un objet JSON pour UNE plongée ---
static esp_err_t build_dive_cjson(const char *dive_id, cJSON **out_obj)
{
    // 1) metadata
    dive_metadata_t meta = {0};
    if (dive_storage_read_metadata(dive_id, &meta) != ESP_OK)
    {
        return ESP_FAIL;
    }

    // 2) data.csv -> samples array
    char path[160];
    build_path(dive_id, "data.csv", path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f)
        return ESP_FAIL;

    // skip header
    char line[192];
    if (fgets(line, sizeof(line), f) == NULL)
    {
        fclose(f);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    if (!root || !arr)
    {
        if (root)
            cJSON_Delete(root);
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "id", meta.id);
    cJSON_AddStringToObject(root, "date", meta.date);
    cJSON_AddStringToObject(root, "location", meta.location);
    cJSON_AddStringToObject(root, "diver", meta.diver);
    cJSON_AddItemToObject(root, "samples", arr);

    while (fgets(line, sizeof(line), f))
    {
        // Format: timestamp_us,temperature_C,pressure_bar
        unsigned long long ts;
        float tc, pb;
        if (sscanf(line, "%llu,%f,%f", &ts, &tc, &pb) == 3)
        {
            cJSON *o = cJSON_CreateObject();
            if (!o)
            {
                cJSON_Delete(root);
                fclose(f);
                return ESP_ERR_NO_MEM;
            }
            cJSON_AddNumberToObject(o, "ts_us", (double)ts);
            cJSON_AddNumberToObject(o, "temp_c", tc);
            cJSON_AddNumberToObject(o, "press_bar", pb);
            cJSON_AddItemToArray(arr, o);
        }
    }
    fclose(f);

    *out_obj = root;
    return ESP_OK;
}

esp_err_t dive_storage_export_dive_json(const char *dive_id, char **out_json, size_t *out_len)
{
    if (!dive_id || !out_json)
        return ESP_ERR_INVALID_ARG;

    cJSON *root = NULL;
    esp_err_t err = build_dive_cjson(dive_id, &root);
    if (err != ESP_OK)
        return err;

    char *txt = cJSON_PrintUnformatted(root); // ou cJSON_Print pour pretty
    cJSON_Delete(root);
    if (!txt)
        return ESP_ERR_NO_MEM;

    *out_json = txt;
    if (out_len)
        *out_len = strlen(txt);
    return ESP_OK;
}

esp_err_t dive_storage_export_all_json(char **out_json, size_t *out_len)
{
    if (!out_json)
        return ESP_ERR_INVALID_ARG;

    // lister les dossiers de plongées
    char ids[64][32]; // jusqu’à 64 dives (ajuste si besoin)
    size_t cnt = 0;
    esp_err_t e = dive_storage_list(ids, 64, &cnt);
    if (e != ESP_OK)
        return e;

    cJSON *arr = cJSON_CreateArray();
    if (!arr)
        return ESP_ERR_NO_MEM;

    for (size_t i = 0; i < cnt; ++i)
    {
        cJSON *obj = NULL;
        if (build_dive_cjson(ids[i], &obj) == ESP_OK && obj)
        {
            cJSON_AddItemToArray(arr, obj);
        }
        // en cas d’erreur sur une dive, on peut choisir de continuer
    }

    char *txt = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    if (!txt)
        return ESP_ERR_NO_MEM;

    *out_json = txt;
    if (out_len)
        *out_len = strlen(txt);
    return ESP_OK;
}