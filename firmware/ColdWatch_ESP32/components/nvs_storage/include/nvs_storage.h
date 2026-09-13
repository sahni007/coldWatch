/*
 * nvs_storage.h
 * Non-volatile storage using the ESP32's real NVS (Non-Volatile Storage) API.
 *  - Persists configurable properties (SRS1_002/003/004/008)
 *  - Maintains a circular event log for alarms (SRS1_006/007/010 "log file")
 */
#ifndef COLDWATCH_NVS_STORAGE_H
#define COLDWATCH_NVS_STORAGE_H

#include <stdint.h>
#include <stdbool.h>

// ---- Configurable properties, persisted in NVS ----
typedef struct {
    float    temperatureHighLimit;   // SRS1_002
    float    temperatureLowLimit;    // SRS1_003
    float    temperatureResolution;  // SRS1_004
    uint16_t temperatureHysteresis;  // SRS1_008 (sample count)
} coldwatch_config_t;

// ---- Log entry ----
typedef struct {
    int64_t  timestampMs;       // esp_timer_get_time()/1000 at time of event
    uint16_t alarmId;           // 1006 / 1007 / 1010
    uint8_t  eventType;         // 0 = RAISED, 1 = CLEARED
    int16_t  tempTimesHundred;  // temperature * 100 (or INT16_MIN if invalid)
} coldwatch_log_entry_t;

#define LOG_EVENT_RAISED   0
#define LOG_EVENT_CLEARED  1
#define LOG_MAX_ENTRIES    64

// Must be called once at startup (initializes nvs_flash + loads/creates config).
void nvs_storage_init(void);

// Config access (SRS1_002/003/004/008)
coldwatch_config_t *nvs_storage_get_config(void);
void nvs_storage_save_config(void);
void nvs_storage_reset_config_to_defaults(void);
void nvs_storage_set_high_limit(float v);
void nvs_storage_set_low_limit(float v);
void nvs_storage_set_resolution(float v);
void nvs_storage_set_hysteresis(uint16_t v);

// Log file (SRS1_006/007/010 event recording)
void nvs_storage_append_log(uint16_t alarmId, uint8_t eventType, float temperature);
void nvs_storage_dump_log(void); // prints via printf/ESP_LOG

#endif // COLDWATCH_NVS_STORAGE_H

