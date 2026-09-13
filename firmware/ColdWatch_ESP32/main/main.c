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
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/uart.h"

#include "config.h"
#include "nvs_storage.h"
#include "temperature_sensor.h"
#include "alarm_manager.h"
#include "buzzer.h"
#include "lcd_i2c.h"
#include "sms_module.h"

static const char *TAG = "ColdWatch";

static float lastTemperature = NAN;
static bool  lastTempValid = false;

static inline int64_t millis64(void) {
    return esp_timer_get_time() / 1000;
}

// ---------------- Serial command console (Serial Monitor / stdin over UART0) ----------------
// (SRS1_002/003/004/008 - runtime configuration)
// Commands (newline terminated), e.g.:
//   SET HIGH 55.0
//   SET LOW -5.0
//   SET RES 0.5
//   SET HYST 20
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
    } else if (strcmp(line, "GET") == 0) {
        printf("High=%.2f Low=%.2f Res=%.2f Hyst=%u\n",
               cfg->temperatureHighLimit, cfg->temperatureLowLimit,
               cfg->temperatureResolution, cfg->temperatureHysteresis);
    } else if (strcmp(line, "LOG") == 0) {
        nvs_storage_dump_log();
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
    int64_t last_sample_ms = 0;
    int64_t last_lcd_refresh_ms = 0;

    while (1) {
        int64_t now = millis64();

        // ---- Sample the sensor at fixed interval (SRS1_001) ----
        if (now - last_sample_ms >= SENSOR_SAMPLE_INTERVAL_MS) {
            last_sample_ms = now;

            float raw_temp;
            bool ok = temperature_sensor_sample(&raw_temp);
            bool faulted = (temperature_sensor_get_fault_state() == SENSOR_STATE_FAULT);

            if (ok) {
                coldwatch_config_t *cfg = nvs_storage_get_config();
                // SRS1_004/005: snap to configured resolution to stabilize readings
                lastTemperature = temperature_round_to_resolution(raw_temp, cfg->temperatureResolution);
                lastTempValid = true;
            } else {
                lastTempValid = false;
            }

            // SRS1_006/007/008/010/011: evaluate alarms
            alarm_manager_update(lastTemperature, lastTempValid, faulted);
        }

        handle_ack_button();

        // ---- Refresh LCD / buzzer pattern ----
        if (now - last_lcd_refresh_ms >= LCD_REFRESH_INTERVAL_MS) {
            last_lcd_refresh_ms = now;
            alarm_manager_refresh_outputs(temperature_sensor_get_type_name(), lastTemperature, lastTempValid); // SRS1_009
        } else {
            buzzer_update(); // keep buzzer pattern responsive between LCD refreshes
        }

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

    nvs_storage_init();          // SRS1_002/003/004/008 (NVS config load)
    temperature_sensor_init();   // SRS1_001
    buzzer_init();
    lcd_init();
    sms_module_init();
    alarm_manager_init();

    printf("Type GET / LOG / RESET / SET HIGH x / SET LOW x / SET RES x / SET HYST x\n");

    xTaskCreate(console_task, "console_task", 4096, NULL, 5, NULL);
    xTaskCreate(coldwatch_task, "coldwatch_task", 4096, NULL, 5, NULL);
}


