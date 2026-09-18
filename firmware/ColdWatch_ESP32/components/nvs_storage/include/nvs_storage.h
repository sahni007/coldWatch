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
    float    humidityHighLimit;      // SRS2_002
    float    humidityLowLimit;       // SRS2_003
    float    humidityResolution;     // SRS2_004
    uint16_t humidityHysteresis;     // SRS2_008 (sample count)
} coldwatch_config_t;

// ---- Log entry ----
typedef struct {
    int64_t  timestampMs;       // esp_timer_get_time()/1000 at time of event
    uint16_t alarmId;           // 1006 / 1007 / 1010 / 2006 / 2007 / 2010
    uint8_t  eventType;         // 0 = RAISED, 1 = CLEARED
    int16_t  tempTimesHundred;  // temperature or humidity * 100 (or INT16_MIN if invalid)
} coldwatch_log_entry_t;

#define LOG_EVENT_RAISED   0
#define LOG_EVENT_CLEARED  1
#define LOG_MAX_ENTRIES    64

// ---- SRS3_006: Emergency data snapshot ----
// Saved to NVS when the battery reaches BATTERY_CRITICAL_PERCENT, so the
// last known readings survive a possible imminent power-off/brown-out.
typedef struct {
    int64_t timestampMs;
    float   temperature;
    uint8_t temperatureValid;   // 1 = 'temperature' is a real reading
    float   humidity;
    uint8_t humidityValid;      // 1 = 'humidity' is a real reading
    float   batteryVoltage;
    uint8_t batteryPercentage;
    uint8_t onBattery;          // 1 = running on internal battery at snapshot time
} coldwatch_emergency_snapshot_t;

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

// Humidity config access (SRS2_002/003/004/008)
void nvs_storage_set_humidity_high_limit(float v);
void nvs_storage_set_humidity_low_limit(float v);
void nvs_storage_set_humidity_resolution(float v);
void nvs_storage_set_humidity_hysteresis(uint16_t v);

// Log file (SRS1_006/007/010, SRS2_006/007/010 event recording)
void nvs_storage_append_log(uint16_t alarmId, uint8_t eventType, float temperature);
void nvs_storage_dump_log(void); // prints via printf/ESP_LOG

// SRS3_006: emergency snapshot (persisted last-known state on critical battery)
void nvs_storage_save_emergency_snapshot(const coldwatch_emergency_snapshot_t *snap);
// Returns true if a previously-saved snapshot was found and loaded into 'out_snap'.
bool nvs_storage_load_emergency_snapshot(coldwatch_emergency_snapshot_t *out_snap);
void nvs_storage_dump_emergency_snapshot(void); // prints via printf, for the "SNAP" console command

#endif // COLDWATCH_NVS_STORAGE_H

