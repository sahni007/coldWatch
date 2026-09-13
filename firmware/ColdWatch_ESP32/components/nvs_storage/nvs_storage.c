/*
 * nvs_storage.c
 */
#include "nvs_storage.h"
#include "config.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "NVS_STORAGE";
#define NVS_NAMESPACE   "coldwatch"
#define KEY_CONFIG      "cfg"
#define KEY_LOG_IDX     "log_idx"

static coldwatch_config_t s_config;
static uint16_t s_log_write_index = 0;
static nvs_handle_t s_handle;

static void load_or_init_config(void) {
    size_t required_size = sizeof(coldwatch_config_t);
    esp_err_t err = nvs_get_blob(s_handle, KEY_CONFIG, &s_config, &required_size);
    if (err != ESP_OK || required_size != sizeof(coldwatch_config_t)) {
        ESP_LOGW(TAG, "Config not found / invalid (err=%s) -> loading defaults", esp_err_to_name(err));
        nvs_storage_reset_config_to_defaults();
    } else {
        ESP_LOGI(TAG, "Config loaded from NVS: high=%.1f low=%.1f res=%.2f hyst=%u",
                 s_config.temperatureHighLimit, s_config.temperatureLowLimit,
                 s_config.temperatureResolution, s_config.temperatureHysteresis);
    }
}

void nvs_storage_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition truncated/corrupt or format changed -> erase and retry
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_handle));

    load_or_init_config();

    size_t idx_size = sizeof(s_log_write_index);
    err = nvs_get_blob(s_handle, KEY_LOG_IDX, &s_log_write_index, &idx_size);
    if (err != ESP_OK || s_log_write_index >= LOG_MAX_ENTRIES) {
        s_log_write_index = 0;
        nvs_set_blob(s_handle, KEY_LOG_IDX, &s_log_write_index, sizeof(s_log_write_index));
        nvs_commit(s_handle);
    }
}

coldwatch_config_t *nvs_storage_get_config(void) {
    return &s_config;
}

void nvs_storage_save_config(void) {
    ESP_ERROR_CHECK(nvs_set_blob(s_handle, KEY_CONFIG, &s_config, sizeof(s_config)));
    ESP_ERROR_CHECK(nvs_commit(s_handle));
}

void nvs_storage_reset_config_to_defaults(void) {
    s_config.temperatureHighLimit  = DEFAULT_TEMP_HIGH_LIMIT;    // SRS1_002
    s_config.temperatureLowLimit   = DEFAULT_TEMP_LOW_LIMIT;     // SRS1_003
    s_config.temperatureResolution = DEFAULT_TEMP_RESOLUTION;    // SRS1_004
    s_config.temperatureHysteresis = DEFAULT_TEMP_HYSTERESIS_CNT;// SRS1_008
    nvs_storage_save_config();
}

void nvs_storage_set_high_limit(float v)     { s_config.temperatureHighLimit = v; nvs_storage_save_config(); }
void nvs_storage_set_low_limit(float v)      { s_config.temperatureLowLimit = v; nvs_storage_save_config(); }
void nvs_storage_set_resolution(float v)     { s_config.temperatureResolution = v; nvs_storage_save_config(); }
void nvs_storage_set_hysteresis(uint16_t v)  { s_config.temperatureHysteresis = v; nvs_storage_save_config(); }

void nvs_storage_append_log(uint16_t alarmId, uint8_t eventType, float temperature) {
    coldwatch_log_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.timestampMs = esp_timer_get_time() / 1000;
    entry.alarmId = alarmId;
    entry.eventType = eventType;
    entry.tempTimesHundred = (temperature == temperature) /* not NaN */
                                 ? (int16_t)(temperature * 100.0f)
                                 : (int16_t)0x8000;

    char key[16];
    snprintf(key, sizeof(key), "log_%u", (unsigned)s_log_write_index);
    nvs_set_blob(s_handle, key, &entry, sizeof(entry));

    s_log_write_index = (s_log_write_index + 1) % LOG_MAX_ENTRIES; // circular buffer
    nvs_set_blob(s_handle, KEY_LOG_IDX, &s_log_write_index, sizeof(s_log_write_index));
    nvs_commit(s_handle);

    ESP_LOGI(TAG, "[LOG] alarm=%u event=%s temp=%.2f t=%lldms",
              alarmId, eventType == LOG_EVENT_RAISED ? "RAISED" : "CLEARED",
              temperature, entry.timestampMs);
}

void nvs_storage_dump_log(void) {
    printf("---- ColdWatch NVS Log Dump ----\n");
    for (uint16_t i = 0; i < LOG_MAX_ENTRIES; i++) {
        char key[16];
        snprintf(key, sizeof(key), "log_%u", (unsigned)i);
        coldwatch_log_entry_t entry;
        size_t sz = sizeof(entry);
        esp_err_t err = nvs_get_blob(s_handle, key, &entry, &sz);
        if (err != ESP_OK || entry.alarmId == 0) continue; // empty slot
        printf("%u: t=%lldms alarm=%u event=%s temp=%.2f\n",
               i, entry.timestampMs, entry.alarmId,
               entry.eventType == LOG_EVENT_RAISED ? "RAISED" : "CLEARED",
               entry.tempTimesHundred / 100.0f);
    }
    printf("---------------------------------\n");
}

