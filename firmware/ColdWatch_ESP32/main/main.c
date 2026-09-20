/*
 * main.c
 * ColdWatch firmware entry point for ESP32 (ESP-IDF, pure C).
 *
 * Requirement coverage:
 *  SRS1_001 - temperature_sensor reads via bit-banged OneWire (DS18B20)
 *             or ESP32 ADC (LM35/NTC)
 *  SRS1_002 - config.temperatureHighLimit (NVS, default 50.0)
 *  SRS1_003 - config.temperatureLowLimit  (NVS, default 0.0)
 *  SRS1_004 - config.temperatureResolution (NVS, default 0.1)
 *  SRS1_005 - DS18B20 12-bit + resolution rounding -> +-1C target accuracy
 *  SRS1_006 - Alarm 1006 (high temp): LCD + SMS + NVS log + buzzer
 *  SRS1_007 - Alarm 1007 (low temp):  LCD + SMS + NVS log + buzzer
 *  SRS1_008 - config.temperatureHysteresis (consecutive-sample debounce)
 *  SRS1_009 - LCD shows active sensor type name
 *  SRS1_010 - Alarm 1010 (sensor fault): LCD + SMS + NVS log + buzzer
 *  SRS1_011 - Alarms 1007 & 1010 auto-clear when condition resolves
 *
 *  SRS2_001 - humidity_sensor reads via the DHT11's bit-banged single-wire
 *             protocol (see components/dht11 + components/humidity_sensor)
 *  SRS2_002 - config.humidityHighLimit (NVS, default 100.0)
 *  SRS2_003 - config.humidityLowLimit  (NVS, default 0.0)
 *  SRS2_004 - config.humidityResolution (NVS, default 0.1)
 *  SRS2_005 - target +-3RH accuracy (see README for DHT11 accuracy caveat)
 *  SRS2_006 - Alarm 2006 (high humidity): LCD + SMS + NVS log + buzzer
 *  SRS2_007 - Alarm 2007 (low humidity):  LCD + SMS + NVS log + buzzer
 *  SRS2_008 - config.humidityHysteresis (consecutive-sample debounce)
 *  SRS2_009 - LCD shows active humidity sensor type name (DHT11)
 *  SRS2_010 - Alarm 2010 (humidity sensor fault): LCD + SMS + NVS log + buzzer
 *  SRS2_011 - Alarms 2006/2007/2010 auto-clear when condition resolves
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "driver/gpio.h"
#include "driver/uart.h"

#include "config.h"
#include "nvs_storage.h"
#include "temperature_sensor.h"
#include "humidity_sensor.h"
#include "alarm_manager.h"
#include "buzzer.h"
#include "lcd_i2c.h"
#include "sms_module.h"
#include "power_source.h"
#include "touch_input.h"

static const char *TAG = "ColdWatch";

static float lastTemperature = NAN;
static bool  lastTempValid = false;
static float lastHumidity = NAN;
static bool  lastHumidityValid = false;

// ---------------- Multi-screen touch UI (HOME / ALARMS / POWER / GSM) ----------------
typedef enum {
    SCREEN_HOME = 0,
    SCREEN_ALARMS,
    SCREEN_POWER,
    SCREEN_GSM,
    SCREEN_COUNT
} screen_state_t;

static screen_state_t s_current_screen = SCREEN_HOME;

static const char *gsm_state_name(gsm_state_t state) {
    switch (state) {
        case GSM_STATE_REGISTERED:     return "REGISTERED";
        case GSM_STATE_NOT_REGISTERED: return "SEARCHING";
        case GSM_STATE_INIT_FAILED:    return "INIT FAILED";
        default:                       return "UNKNOWN";
    }
}

static inline int64_t millis64(void) {
    return esp_timer_get_time() / 1000;
}

// Reads the touch panel and cycles s_current_screen when the bottom nav bar
// is tapped (see config.h NAV_BAR_Y_TOP/NAV_PREV_X_MAX/NAV_NEXT_X_MIN). Touch
// is only consulted here - while any alarm is active, alarm_manager_refresh_
// outputs() takes over the display entirely and this selection is simply
// not drawn until the alarm clears (screen choice is preserved, not lost).
static void handle_touch_navigation(void) {
    uint16_t x, y;
    if (!touch_input_get_point(&x, &y)) {
        return; // no new tap this cycle
    }
    ESP_LOGI(TAG, "[TOUCH] tap x=%u y=%u", (unsigned)x, (unsigned)y);
    if (y < NAV_BAR_Y_TOP) {
        ESP_LOGI(TAG, "[TOUCH] ignored: above navigation bar");
        return; // tap was inside the screen content, not the nav bar
    }
    if (x < NAV_PREV_X_MAX) {
        s_current_screen = (screen_state_t)((s_current_screen + SCREEN_COUNT - 1) % SCREEN_COUNT);
        ESP_LOGI(TAG, "[TOUCH] PREV -> screen %d", s_current_screen);
    } else if (x >= NAV_NEXT_X_MIN) {
        s_current_screen = (screen_state_t)((s_current_screen + 1) % SCREEN_COUNT);
        ESP_LOGI(TAG, "[TOUCH] NEXT -> screen %d", s_current_screen);
    } else {
        ESP_LOGI(TAG, "[TOUCH] ignored: center of navigation bar");
    }
}

// ---------------- Serial command console (Serial Monitor / stdin over UART0) ----------------
// (SRS1_002/003/004/008, SRS2_002/003/004/008 - runtime configuration)
// Commands (newline terminated), e.g.:
//   SET HIGH 55.0
//   SET LOW -5.0
//   SET RES 0.5
//   SET HYST 20
//   SET HUMHIGH 80.0
//   SET HUMLOW 10.0
//   SET HUMRES 0.5
//   SET HUMHYST 20
//   GET
//   LOG
//   RESET
static void process_console_command(const char *line) {
    coldwatch_config_t *cfg = nvs_storage_get_config();

    if (strncmp(line, "SET HIGH ", 9) == 0) {
        nvs_storage_set_high_limit(atof(line + 9));
        printf("OK: temperatureHighLimit updated\n");
    } else if (strncmp(line, "SET LOW ", 8) == 0) {
        nvs_storage_set_low_limit(atof(line + 8));
        printf("OK: temperatureLowLimit updated\n");
    } else if (strncmp(line, "SET RES ", 8) == 0) {
        nvs_storage_set_resolution(atof(line + 8));
        printf("OK: temperatureResolution updated\n");
    } else if (strncmp(line, "SET HYST ", 9) == 0) {
        nvs_storage_set_hysteresis((uint16_t)atoi(line + 9));
        printf("OK: temperatureHysteresis updated\n");
    } else if (strncmp(line, "SET HUMHIGH ", 12) == 0) {
        nvs_storage_set_humidity_high_limit(atof(line + 12));
        printf("OK: humidityHighLimit updated\n");
    } else if (strncmp(line, "SET HUMLOW ", 11) == 0) {
        nvs_storage_set_humidity_low_limit(atof(line + 11));
        printf("OK: humidityLowLimit updated\n");
    } else if (strncmp(line, "SET HUMRES ", 11) == 0) {
        nvs_storage_set_humidity_resolution(atof(line + 11));
        printf("OK: humidityResolution updated\n");
    } else if (strncmp(line, "SET HUMHYST ", 12) == 0) {
        nvs_storage_set_humidity_hysteresis((uint16_t)atoi(line + 12));
        printf("OK: humidityHysteresis updated\n");
    } else if (strcmp(line, "GET") == 0) {
        printf("High=%.2f Low=%.2f Res=%.2f Hyst=%u | "
               "HumHigh=%.2f HumLow=%.2f HumRes=%.2f HumHyst=%u\n",
               cfg->temperatureHighLimit, cfg->temperatureLowLimit,
               cfg->temperatureResolution, cfg->temperatureHysteresis,
               cfg->humidityHighLimit, cfg->humidityLowLimit,
               cfg->humidityResolution, cfg->humidityHysteresis);
    } else if (strcmp(line, "LOG") == 0) {
        nvs_storage_dump_log();
    } else if (strcmp(line, "POWER") == 0) {
        printf("Power source: %s (last sensed voltage: %.2fV on GPIO%u) | "
               "Battery: %u%% (%.2fV on GPIO%u) [%s]\n",
               power_source_get_state_name(), power_source_get_last_voltage(),
               (unsigned)POWER_SOURCE_GPIO,
               power_source_get_battery_percentage(), power_source_get_battery_voltage(),
               (unsigned)BATTERY_GPIO,
               power_source_battery_reading_is_accurate() ? "ACCURATE" : "ESTIMATE/STATUS-ONLY");
    } else if (strcmp(line, "SNAP") == 0) {
        nvs_storage_dump_emergency_snapshot();
    } else if (strcmp(line, "RESET") == 0) {
        nvs_storage_reset_config_to_defaults();
        printf("OK: config reset to defaults\n");
    } else if (strlen(line) > 0) {
        printf("ERR: unknown command\n");
    }
}

static void console_task(void *arg) {
    char buffer[64];
    size_t idx = 0;

    while (1) {
        uint8_t c;
        int len = uart_read_bytes(UART_NUM_0, &c, 1, pdMS_TO_TICKS(50));
        if (len > 0) {
            if (c == '\n' || c == '\r') {
                buffer[idx] = '\0';
                if (idx > 0) process_console_command(buffer);
                idx = 0;
            } else if (idx < sizeof(buffer) - 1) {
                buffer[idx++] = (char)c;
            }
        }
    }
}

// ---------------- Manual acknowledge / silence button ----------------
static void handle_ack_button(void) {
    static int last_state = 1; // HIGH (idle, pull-up)
    int state = gpio_get_level(ACK_BUTTON_GPIO);
    if (last_state == 1 && state == 0) {
        // Button pressed: silence buzzer immediately (does not clear the
        // underlying alarm condition/state, only stops the audible tone).
        buzzer_stop();
        ESP_LOGI(TAG, "[ACK] Buzzer silenced by user");
    }
    last_state = state;
}

// ---------------- Main application task ----------------
static void coldwatch_task(void *arg) {
    esp_task_wdt_user_handle_t wdt_user = NULL;
    int64_t last_lcd_refresh_ms = 0;
    int64_t last_humidity_sample_ms = 0;
    int64_t last_power_sample_ms = 0;
    int64_t last_gsm_poll_ms = 0;

    ESP_ERROR_CHECK(esp_task_wdt_add_user("coldwatch_task", &wdt_user));
    ESP_LOGI(TAG, "Application watchdog enabled for coldwatch_task");

    while (1) {
        int64_t now = millis64();

        // ---- Sample the main power-supply detector (GPIO34) ----
        // ~3.3V => mains power present, ~0V => running on internal battery.
        // power_source_update() applies its own debounce window internally
        // (POWER_SOURCE_DEBOUNCE_WINDOW_MS).
        if (now - last_power_sample_ms >= POWER_SOURCE_SAMPLE_INTERVAL_MS) {
            last_power_sample_ms = now;
            power_source_update();
            alarm_manager_update_power(power_source_is_battery());
            // SRS3_004/005/006: battery voltage/percentage monitoring
            alarm_manager_update_battery(power_source_get_battery_voltage(),
                                          power_source_get_battery_percentage(),
                                          power_source_is_battery(),
                                          lastTemperature, lastTempValid,
                                          lastHumidity, lastHumidityValid);
        }

        // ---- Poll GSM module status for the GSM screen ----
        if (now - last_gsm_poll_ms >= GSM_STATUS_POLL_INTERVAL_MS) {
            last_gsm_poll_ms = now;
            sms_module_poll_status();
        }

        // ---- Sample the humidity sensor at fixed interval (SRS2_001) ----
        if (now - last_humidity_sample_ms >= DHT11_SAMPLE_INTERVAL_MS) {
            last_humidity_sample_ms = now;

            float raw_humidity_temperature;
            float raw_humidity;
            bool hum_ok = humidity_sensor_sample(&raw_humidity_temperature, &raw_humidity);
            bool hum_faulted = (humidity_sensor_get_fault_state() == HUMIDITY_SENSOR_STATE_FAULT);

            if (hum_ok) {
                coldwatch_config_t *cfg = nvs_storage_get_config();
                // DHT11 provides both values, so use it as the connected
                // temperature and humidity source.
                lastTemperature = temperature_round_to_resolution(
                    raw_humidity_temperature, cfg->temperatureResolution);
                lastTempValid = true;
                lastHumidity = humidity_round_to_resolution(raw_humidity, cfg->humidityResolution);
                lastHumidityValid = true;
                ESP_LOGI(TAG, "DHT11 temperature: %.1f C, humidity: %.1f %%RH",
                         lastTemperature, lastHumidity);
            } else {
                lastTempValid = false;
                lastHumidityValid = false;
                ESP_LOGW(TAG, "Humidity read failed (fault state: %s)",
                         hum_faulted ? "FAULT" : "DEBOUNCING");
            }

            // Evaluate both alarms from the same DHT11 sample.
            alarm_manager_update(lastTemperature, lastTempValid, false);
            alarm_manager_update_humidity(lastHumidity, lastHumidityValid, hum_faulted);
        }

        handle_ack_button();
        handle_touch_navigation(); // updates s_current_screen on a nav-bar tap

        // ---- Refresh LCD / buzzer pattern ----
        if (now - last_lcd_refresh_ms >= LCD_REFRESH_INTERVAL_MS) {
            last_lcd_refresh_ms = now;

            // NOTE: alarm_manager_refresh_outputs() still drives the buzzer/
            // SMS/NVS logging exactly as before (SRS1_006/007/010,
            // SRS2_006/007/010, SRS3_001/003/004/005/006) - it just no
            // longer draws its own LCD screen. Alarm DETAILS are only ever
            // shown on the dedicated ALARMS screen (lcd_show_alarms_screen);
            // HOME/POWER/GSM always render their own normal content,
            // whatever the current alarm state is.
            (void)alarm_manager_refresh_outputs(
                humidity_sensor_get_type_name(), lastTemperature, lastTempValid,
                humidity_sensor_get_type_name(), lastHumidity, lastHumidityValid,
                power_source_get_state_name(),
                power_source_get_battery_voltage(),
                power_source_get_battery_percentage());

            {
                // Always render whichever screen touch last selected - an
                // active alarm no longer forces a different screen.
                switch (s_current_screen) {
                    case SCREEN_HOME: {
                        uint8_t active_count = alarm_manager_get_active_count();
                        lcd_show_home(humidity_sensor_get_type_name(), lastTemperature, lastTempValid,
                                      humidity_sensor_get_type_name(), lastHumidity, lastHumidityValid,
                                      active_count);
                        break;
                    }
                    case SCREEN_ALARMS: {
                        uint16_t ids[ALARMS_SCREEN_MAX_VISIBLE];
                        const char *names[ALARMS_SCREEN_MAX_VISIBLE];
                        uint8_t active_count = alarm_manager_get_active_count();
                        uint8_t listed = alarm_manager_get_active_list(ids, names, ALARMS_SCREEN_MAX_VISIBLE);
                        lcd_show_alarms_screen(active_count, ids, names, listed);
                        break;
                    }
                    case SCREEN_POWER:
                        lcd_show_power_screen(power_source_get_state_name(),
                                              power_source_get_battery_voltage(),
                                              power_source_get_battery_percentage(),
                                              alarm_manager_is_battery_low_active(),
                                              alarm_manager_is_battery_critical_active());
                        break;
                    case SCREEN_GSM:
                    default:
                        lcd_show_gsm_screen(gsm_state_name(sms_module_get_state()),
                                            sms_module_get_signal_quality(),
                                            sms_module_get_has_sent_any(),
                                            sms_module_get_last_send_ok());
                        break;
                }
            }
        } else {
            buzzer_update(); // keep buzzer pattern responsive between LCD refreshes
        }

        ESP_ERROR_CHECK(esp_task_wdt_reset_user(wdt_user));
        vTaskDelay(pdMS_TO_TICKS(BUZZER_UPDATE_INTERVAL_MS));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "=== ColdWatch Firmware Boot (ESP32) ===");

    // Configure UART0 for the console command interface (Serial Monitor)
    uart_driver_install(UART_NUM_0, 256 * 2, 0, 0, NULL, 0);

    gpio_reset_pin(ACK_BUTTON_GPIO);
    gpio_set_direction(ACK_BUTTON_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(ACK_BUTTON_GPIO, GPIO_PULLUP_ONLY);

    nvs_storage_init();          // SRS1_002/003/004/008, SRS2_002/003/004/008 (NVS config load)
    temperature_sensor_init();   // SRS1_001
    humidity_sensor_init();      // SRS2_001
    power_source_init();         // Main power-supply detection (GPIO34) + battery monitoring (GPIO35)
    buzzer_init();
    lcd_init();
    touch_input_init();          // XPT2046 touch nav for the 4-screen UI (shares the LCD's SPI bus)
    sms_module_init();
    alarm_manager_init();

    // SRS3_006: report any emergency snapshot saved during a previous
    // critical-battery event, so it can be recovered/inspected after reboot.
    {
        coldwatch_emergency_snapshot_t snap;
        if (nvs_storage_load_emergency_snapshot(&snap)) {
            ESP_LOGW(TAG, "Found emergency snapshot from a previous critical-battery event "
                          "(t=%lldms, batt=%u%%) - type SNAP to view",
                     snap.timestampMs, snap.batteryPercentage);
        }
    }

    printf("Type GET / LOG / POWER / SNAP / RESET / SET HIGH x / SET LOW x / SET RES x / SET HYST x /\n"
           "     SET HUMHIGH x / SET HUMLOW x / SET HUMRES x / SET HUMHYST x\n");
    printf("LCD: tap the bottom-left/right nav bar to switch HOME / ALARMS / POWER / GSM screens.\n");

    xTaskCreate(console_task, "console_task", 4096, NULL, 5, NULL);
    xTaskCreate(coldwatch_task, "coldwatch_task", 4096, NULL, 5, NULL);
}


