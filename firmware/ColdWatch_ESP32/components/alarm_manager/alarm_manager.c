/*
 * alarm_manager.c
 */
#include "alarm_manager.h"
#include "config.h"
#include "nvs_storage.h"
#include "buzzer.h"
#include "lcd_i2c.h"
#include "sms_module.h"
#include "esp_timer.h"
#include <math.h>
#include <stdio.h>

typedef enum { ALARM_STATE_CLEARED = 0, ALARM_STATE_RAISED = 1 } alarm_state_t;

static alarm_state_t s_high_state  = ALARM_STATE_CLEARED;
static alarm_state_t s_low_state   = ALARM_STATE_CLEARED;
static alarm_state_t s_fault_state = ALARM_STATE_CLEARED;

static uint16_t s_high_raise_cnt = 0, s_high_clear_cnt = 0;
static uint16_t s_low_raise_cnt  = 0, s_low_clear_cnt  = 0;

// SRS2_006/007/010: humidity alarm state (mirrors the temperature state above)
static alarm_state_t s_humidity_high_state  = ALARM_STATE_CLEARED;
static alarm_state_t s_humidity_low_state   = ALARM_STATE_CLEARED;
static alarm_state_t s_humidity_fault_state = ALARM_STATE_CLEARED;

static uint16_t s_humidity_high_raise_cnt = 0, s_humidity_high_clear_cnt = 0;
static uint16_t s_humidity_low_raise_cnt  = 0, s_humidity_low_clear_cnt  = 0;

// Main power-supply loss state (GPIO34 detection, see components/power_source)
static alarm_state_t s_power_state = ALARM_STATE_CLEARED; // CLEARED = mains power OK

// SRS3_005/006: battery low / critical state (see components/power_source)
static alarm_state_t s_battery_low_state      = ALARM_STATE_CLEARED;
static alarm_state_t s_battery_critical_state = ALARM_STATE_CLEARED;
static uint16_t s_battery_low_raise_cnt = 0, s_battery_low_clear_cnt = 0;
static uint16_t s_battery_critical_raise_cnt = 0, s_battery_critical_clear_cnt = 0;

// Last-known power source / battery readings, cached so lcd_show_alarm()
// always has something to show on the LCD even while an alarm is active
// (see alarm_manager_update_battery / alarm_manager_refresh_outputs).
static const char *s_last_power_source_name = "--";
static float       s_last_battery_voltage    = 0.0f;
static uint8_t     s_last_battery_percentage = 0;

void alarm_manager_init(void) {
    // Nothing to do yet; kept for symmetry / future extension.
}

static void notify(uint16_t alarm_id, uint8_t event_type, float temperature,
                    const char *lcd_line, const char *sms_message,
                    uint16_t buzzer_freq, bool start_buzzer) {
    // Log to NVS (SRS1_006/007/010 "Record the event in a log file")
    nvs_storage_append_log(alarm_id, event_type, temperature);

        printf("[ALARM] %s ID=%u temperature=%.1f C\n",
            event_type == LOG_EVENT_RAISED ? "RAISED" : "CLEARED",
            alarm_id, temperature);

    (void)lcd_line;

    // Send SMS (SRS1_006/007/010) - only on RAISE, not on clear, to limit SMS cost/spam.
    if (event_type == LOG_EVENT_RAISED) {
        sms_module_send(sms_message);
    }

    // Buzzer (SRS1_006/007 "activate the buzzer at a defined frequency")
    if (start_buzzer) {
        buzzer_start(buzzer_freq);
    } else {
        buzzer_stop();
    }
}

// ---------------- High temp: Alarm 1006 ----------------
static void raise_high(float t) {
    s_high_state = ALARM_STATE_RAISED;
    notify(ALARM_ID_HIGH_TEMP, LOG_EVENT_RAISED, t,
           "HIGH TEMP!", "ColdWatch ALARM 1006: High temperature detected",
           BUZZER_FREQ_HIGH_ALARM_HZ, true);
}
static void clear_high(float t) {
    s_high_state = ALARM_STATE_CLEARED;
    notify(ALARM_ID_HIGH_TEMP, LOG_EVENT_CLEARED, t, "", "", 0, false);
}

// ---------------- Low temp: Alarm 1007 ----------------
static void raise_low(float t) {
    s_low_state = ALARM_STATE_RAISED;
    notify(ALARM_ID_LOW_TEMP, LOG_EVENT_RAISED, t,
           "LOW TEMP!", "ColdWatch ALARM 1007: Low temperature detected",
           BUZZER_FREQ_LOW_ALARM_HZ, true);
}
static void clear_low(float t) {
    s_low_state = ALARM_STATE_CLEARED; // SRS1_011
    notify(ALARM_ID_LOW_TEMP, LOG_EVENT_CLEARED, t, "", "", 0, false);
}

// ---------------- Sensor fault: Alarm 1010 ----------------
static void raise_fault(void) {
    s_fault_state = ALARM_STATE_RAISED;
    notify(ALARM_ID_SENSOR_FAULT, LOG_EVENT_RAISED, NAN,
           "SENSOR FAULT!", "ColdWatch ALARM 1010: Temperature sensor fault (check connection)",
           BUZZER_FREQ_FAULT_ALARM_HZ, true);
}
static void clear_fault(void) {
    s_fault_state = ALARM_STATE_CLEARED; // SRS1_011
    notify(ALARM_ID_SENSOR_FAULT, LOG_EVENT_CLEARED, NAN, "", "", 0, false);
}

// ---------------- High humidity: Alarm 2006 ----------------
static void raise_humidity_high(float h) {
    s_humidity_high_state = ALARM_STATE_RAISED;
    notify(ALARM_ID_HIGH_HUMIDITY, LOG_EVENT_RAISED, h,
           "HIGH HUMIDITY!", "ColdWatch ALARM 2006: High humidity detected",
           BUZZER_FREQ_HIGH_HUMIDITY_ALARM_HZ, true);
}
static void clear_humidity_high(float h) {
    s_humidity_high_state = ALARM_STATE_CLEARED;
    notify(ALARM_ID_HIGH_HUMIDITY, LOG_EVENT_CLEARED, h, "", "", 0, false);
}

// ---------------- Low humidity: Alarm 2007 ----------------
static void raise_humidity_low(float h) {
    s_humidity_low_state = ALARM_STATE_RAISED;
    notify(ALARM_ID_LOW_HUMIDITY, LOG_EVENT_RAISED, h,
           "LOW HUMIDITY!", "ColdWatch ALARM 2007: Low humidity detected",
           BUZZER_FREQ_LOW_HUMIDITY_ALARM_HZ, true);
}
static void clear_humidity_low(float h) {
    s_humidity_low_state = ALARM_STATE_CLEARED; // SRS2_011
    notify(ALARM_ID_LOW_HUMIDITY, LOG_EVENT_CLEARED, h, "", "", 0, false);
}

// ---------------- Humidity sensor fault: Alarm 2010 ----------------
static void raise_humidity_fault(void) {
    s_humidity_fault_state = ALARM_STATE_RAISED;
    notify(ALARM_ID_HUMIDITY_SENSOR_FAULT, LOG_EVENT_RAISED, NAN,
           "HUM SENSOR FAULT", "ColdWatch ALARM 2010: Humidity sensor fault (check connection)",
           BUZZER_FREQ_HUMIDITY_FAULT_ALARM_HZ, true);
}
static void clear_humidity_fault(void) {
    s_humidity_fault_state = ALARM_STATE_CLEARED; // SRS2_011
    notify(ALARM_ID_HUMIDITY_SENSOR_FAULT, LOG_EVENT_CLEARED, NAN, "", "", 0, false);
}

// ---------------- Main power supply lost/restored: Alarm 3001 (SRS3_001/003) ----------------
static void raise_power_lost(void) {
    s_power_state = ALARM_STATE_RAISED;
    notify(ALARM_ID_POWER_SOURCE, LOG_EVENT_RAISED, NAN,
           "ON BATTERY!", "ColdWatch ALARM 3001: Main power lost - running on internal battery",
           BUZZER_FREQ_POWER_ALARM_HZ, true);
}
static void clear_power_restored(void) {
    s_power_state = ALARM_STATE_CLEARED;
    notify(ALARM_ID_POWER_SOURCE, LOG_EVENT_CLEARED, NAN,
           "", "ColdWatch: Main power supply restored", 0, false);
}

// SRS3_001/003: debouncing is already applied upstream in power_source_update();
// we simply react to confirmed transitions here.
void alarm_manager_update_power(bool on_battery) {
    if (on_battery && s_power_state == ALARM_STATE_CLEARED) {
        raise_power_lost();          // SRS3_001
    } else if (!on_battery && s_power_state == ALARM_STATE_RAISED) {
        clear_power_restored();      // SRS3_003
    }
}

// ---------------- Low battery: Alarm 3005 (SRS3_005) ----------------
static void raise_battery_low(uint8_t percent) {
    s_battery_low_state = ALARM_STATE_RAISED;
    char msg[64];
    snprintf(msg, sizeof(msg), "ColdWatch ALARM 3005: Low battery (%u%%)", percent);
    notify(ALARM_ID_LOW_BATTERY, LOG_EVENT_RAISED, (float)percent,
           "LOW BATTERY!", msg, BUZZER_FREQ_LOW_BATTERY_ALARM_HZ, true);
}
static void clear_battery_low(uint8_t percent) {
    s_battery_low_state = ALARM_STATE_CLEARED;
    notify(ALARM_ID_LOW_BATTERY, LOG_EVENT_CLEARED, (float)percent, "", "", 0, false);
}

// ---------------- Critical battery: Alarm 3006 (SRS3_006) ----------------
static void raise_battery_critical(uint8_t percent, float voltage, bool on_battery,
                                    float last_temperature, bool last_temp_valid,
                                    float last_humidity, bool last_humidity_valid) {
    s_battery_critical_state = ALARM_STATE_RAISED;
    char msg[64];
    snprintf(msg, sizeof(msg), "ColdWatch ALARM 3006: CRITICAL battery (%u%%) - data saved", percent);
    notify(ALARM_ID_CRITICAL_BATTERY, LOG_EVENT_RAISED, (float)percent,
           "BATT CRITICAL!", msg, BUZZER_FREQ_CRITICAL_BATTERY_ALARM_HZ, true);

    // SRS3_006: save important data in memory (NVS) before a possible
    // imminent power-off/brown-out.
    coldwatch_emergency_snapshot_t snap = {
        .timestampMs        = esp_timer_get_time() / 1000,
        .temperature         = last_temperature,
        .temperatureValid    = last_temp_valid ? 1 : 0,
        .humidity            = last_humidity,
        .humidityValid       = last_humidity_valid ? 1 : 0,
        .batteryVoltage      = voltage,
        .batteryPercentage   = percent,
        .onBattery           = on_battery ? 1 : 0,
    };
    nvs_storage_save_emergency_snapshot(&snap);
}
static void clear_battery_critical(uint8_t percent) {
    s_battery_critical_state = ALARM_STATE_CLEARED;
    notify(ALARM_ID_CRITICAL_BATTERY, LOG_EVENT_CLEARED, (float)percent, "", "", 0, false);
}

// SRS3_004/005/006: battery evaluation with consecutive-sample debounce,
// mirroring the temperature/humidity hysteresis pattern above.
void alarm_manager_update_battery(float voltage, uint8_t percent, bool on_battery,
                                   float last_temperature, bool last_temp_valid,
                                   float last_humidity, bool last_humidity_valid) {
    // Cache latest reading so lcd_show_alarm() can always display power/battery
    // status, even while some other alarm screen is being shown (see notify()).
    s_last_power_source_name = on_battery ? "BATTERY" : "MAIN";
    s_last_battery_voltage    = voltage;
    s_last_battery_percentage = percent;

    // --- Critical battery (SRS3_006) ---
    if (percent <= BATTERY_CRITICAL_PERCENT) {
        s_battery_critical_raise_cnt++;
        s_battery_critical_clear_cnt = 0;
        if (s_battery_critical_state == ALARM_STATE_CLEARED &&
            s_battery_critical_raise_cnt >= BATTERY_CRITICAL_HYSTERESIS_CNT) {
            raise_battery_critical(percent, voltage, on_battery,
                                    last_temperature, last_temp_valid,
                                    last_humidity, last_humidity_valid);
        }
    } else {
        s_battery_critical_raise_cnt = 0;
        if (s_battery_critical_state == ALARM_STATE_RAISED) {
            s_battery_critical_clear_cnt++;
            if (s_battery_critical_clear_cnt >= BATTERY_CRITICAL_HYSTERESIS_CNT) {
                clear_battery_critical(percent);
            }
        }
    }

    // --- Low battery (SRS3_005) ---
    if (percent <= BATTERY_LOW_PERCENT) {
        s_battery_low_raise_cnt++;
        s_battery_low_clear_cnt = 0;
        if (s_battery_low_state == ALARM_STATE_CLEARED &&
            s_battery_low_raise_cnt >= BATTERY_LOW_HYSTERESIS_CNT) {
            raise_battery_low(percent);
        }
    } else {
        s_battery_low_raise_cnt = 0;
        if (s_battery_low_state == ALARM_STATE_RAISED) {
            s_battery_low_clear_cnt++;
            if (s_battery_low_clear_cnt >= BATTERY_LOW_HYSTERESIS_CNT) {
                clear_battery_low(percent);
            }
        }
    }
}

// ---------------- Main evaluation ----------------
void alarm_manager_update(float temperature, bool temp_valid, bool sensor_fault) {
    coldwatch_config_t *cfg = nvs_storage_get_config();
    uint16_t hyst = cfg->temperatureHysteresis; // SRS1_008: consecutive-sample debounce count

    // --- Sensor fault (SRS1_010) - debounce already applied inside temperature_sensor ---
    if (sensor_fault) {
        s_high_raise_cnt = s_low_raise_cnt = 0; // temperature is not trustworthy while faulted
        s_high_clear_cnt = s_low_clear_cnt = 0;
        if (s_fault_state == ALARM_STATE_CLEARED) raise_fault();
        return; // don't evaluate high/low limits on invalid data
    } else {
        if (s_fault_state == ALARM_STATE_RAISED) clear_fault(); // SRS1_011
    }

    if (!temp_valid) return;

    // --- High / Low temperature evaluation with hysteresis debounce (SRS1_008) ---
    if (temperature > cfg->temperatureHighLimit) {
        s_high_raise_cnt++;
        s_low_raise_cnt = 0; s_low_clear_cnt = 0;
        s_high_clear_cnt = 0;
        if (s_high_state == ALARM_STATE_CLEARED && s_high_raise_cnt >= hyst) raise_high(temperature);

    } else if (temperature < cfg->temperatureLowLimit) {
        s_low_raise_cnt++;
        s_high_raise_cnt = 0; s_high_clear_cnt = 0;
        s_low_clear_cnt = 0;
        if (s_low_state == ALARM_STATE_CLEARED && s_low_raise_cnt >= hyst) raise_low(temperature);

    } else {
        // Back within normal range
        s_high_raise_cnt = 0;
        s_low_raise_cnt = 0;

#if ENABLE_HIGH_AUTO_CLEAR
        if (s_high_state == ALARM_STATE_RAISED) {
            s_high_clear_cnt++;
            if (s_high_clear_cnt >= hyst) clear_high(temperature);
        }
#endif
        if (s_low_state == ALARM_STATE_RAISED) { // SRS1_011 (mandatory auto-clear)
            s_low_clear_cnt++;
            if (s_low_clear_cnt >= hyst) clear_low(temperature);
        }
    }
}

// ---------------- Main evaluation (humidity, SRS2_006/007/008/010/011) ----------------
void alarm_manager_update_humidity(float humidity, bool humidity_valid, bool sensor_fault) {
    coldwatch_config_t *cfg = nvs_storage_get_config();
    uint16_t hyst = cfg->humidityHysteresis; // SRS2_008: consecutive-sample debounce count

    // --- Sensor fault (SRS2_010) - debounce already applied inside humidity_sensor ---
    if (sensor_fault) {
        s_humidity_high_raise_cnt = s_humidity_low_raise_cnt = 0; // humidity is not trustworthy while faulted
        s_humidity_high_clear_cnt = s_humidity_low_clear_cnt = 0;
        if (s_humidity_fault_state == ALARM_STATE_CLEARED) raise_humidity_fault();
        return; // don't evaluate high/low limits on invalid data
    } else {
        if (s_humidity_fault_state == ALARM_STATE_RAISED) clear_humidity_fault(); // SRS2_011
    }

    if (!humidity_valid) return;

    // --- High / Low humidity evaluation with hysteresis debounce (SRS2_008) ---
    if (humidity > cfg->humidityHighLimit) {
        s_humidity_high_raise_cnt++;
        s_humidity_low_raise_cnt = 0; s_humidity_low_clear_cnt = 0;
        s_humidity_high_clear_cnt = 0;
        if (s_humidity_high_state == ALARM_STATE_CLEARED && s_humidity_high_raise_cnt >= hyst) raise_humidity_high(humidity);

    } else if (humidity < cfg->humidityLowLimit) {
        s_humidity_low_raise_cnt++;
        s_humidity_high_raise_cnt = 0; s_humidity_high_clear_cnt = 0;
        s_humidity_low_clear_cnt = 0;
        if (s_humidity_low_state == ALARM_STATE_CLEARED && s_humidity_low_raise_cnt >= hyst) raise_humidity_low(humidity);

    } else {
        // Back within normal range
        s_humidity_high_raise_cnt = 0;
        s_humidity_low_raise_cnt = 0;

#if ENABLE_HIGH_AUTO_CLEAR
        if (s_humidity_high_state == ALARM_STATE_RAISED) {
            s_humidity_high_clear_cnt++;
            if (s_humidity_high_clear_cnt >= hyst) clear_humidity_high(humidity);
        }
#endif
        if (s_humidity_low_state == ALARM_STATE_RAISED) { // SRS2_011 (mandatory auto-clear)
            s_humidity_low_clear_cnt++;
            if (s_humidity_low_clear_cnt >= hyst) clear_humidity_low(humidity);
        }
    }
}

void alarm_manager_refresh_outputs(const char *sensor_type_name, float last_temperature, bool last_temp_valid,
                                    const char *humidity_sensor_type_name, float last_humidity, bool last_humidity_valid,
                                    const char *power_source_name, float battery_voltage, uint8_t battery_percentage,
                                    bool battery_reading_is_accurate) {
    lcd_show_normal(sensor_type_name, last_temperature, last_temp_valid,
                    humidity_sensor_type_name, last_humidity, last_humidity_valid,
                    power_source_name, battery_voltage, battery_percentage,
                    battery_reading_is_accurate);

    buzzer_update();
}

bool alarm_manager_is_high_active(void)  { return s_high_state  == ALARM_STATE_RAISED; }
bool alarm_manager_is_low_active(void)   { return s_low_state   == ALARM_STATE_RAISED; }
bool alarm_manager_is_fault_active(void) { return s_fault_state == ALARM_STATE_RAISED; }

bool alarm_manager_is_humidity_high_active(void)  { return s_humidity_high_state  == ALARM_STATE_RAISED; }
bool alarm_manager_is_humidity_low_active(void)   { return s_humidity_low_state   == ALARM_STATE_RAISED; }
bool alarm_manager_is_humidity_fault_active(void) { return s_humidity_fault_state == ALARM_STATE_RAISED; }

bool alarm_manager_is_power_lost_active(void) { return s_power_state == ALARM_STATE_RAISED; }

bool alarm_manager_is_battery_low_active(void)      { return s_battery_low_state      == ALARM_STATE_RAISED; }
bool alarm_manager_is_battery_critical_active(void) { return s_battery_critical_state == ALARM_STATE_RAISED; }

