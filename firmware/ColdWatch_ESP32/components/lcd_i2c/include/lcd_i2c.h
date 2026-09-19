/*
 * lcd_i2c.h
 * SRS1_006/007/010, SRS2_006/007/010: "Display the alarm on the LCD"
 * SRS1_009 / SRS2_009: "display the type of sensor on the LCD (supporting
 * multiple sensor types)"
 * ILI9341 320x240 landscape TFT driver over SPI.
 */
#ifndef COLDWATCH_LCD_I2C_H
#define COLDWATCH_LCD_I2C_H

#include <stdint.h>
#include <stdbool.h>

void lcd_init(void);

// ---- Screen 1/4: HOME - project name, date, time, sensor readings, status ----
void lcd_show_home(const char *sensor_type_name, float temperature, bool temp_valid,
                    const char *humidity_sensor_type_name, float humidity, bool humidity_valid,
                    uint8_t active_alarm_count);

// ---- Screen 2/4: ALARMS - how many, and a list of which ones ----
// ids/names must each have room for at least list_count entries (see
// alarm_manager_get_active_list() / ALARMS_SCREEN_MAX_VISIBLE in config.h).
void lcd_show_alarms_screen(uint8_t active_count, const uint16_t *ids,
                             const char *const *names, uint8_t list_count);

// ---- Screen 3/4: POWER - source, battery percentage/voltage, status ----
void lcd_show_power_screen(const char *power_source_name, float battery_voltage,
                            uint8_t battery_percentage, bool battery_low_active,
                            bool battery_critical_active);

// ---- Screen 4/4: GSM - module/network status, signal quality, last SMS result ----
// gsm_state_name: e.g. "REGISTERED" / "SEARCHING" / "INIT FAILED" / "UNKNOWN"
// (computed by main.c from sms_module_get_state(), keeping lcd_i2c decoupled
// from sms_module). signal_quality: 0-31, or -1 if never polled.
void lcd_show_gsm_screen(const char *gsm_state_name, int8_t signal_quality,
                          bool has_sent_sms, bool last_send_ok);

// ---- Alarm override screen ----
// Takes priority over all 4 screens above whenever any alarm is active
// (SRS1_006/007/010, SRS2_006/007/010, SRS3_001/003/004/005/006): shows the
// SAME full status (date/time/temperature/humidity/power/battery) as the
// HOME screen PLUS an alarm banner, so nothing is hidden during an alarm.
void lcd_show_alarm(uint16_t alarm_id, const char *text,
                     const char *sensor_type_name, float temperature, bool temp_valid,
                     const char *humidity_sensor_type_name, float humidity, bool humidity_valid,
                     const char *power_source_name, float battery_voltage,
                     uint8_t battery_percentage);

#endif // COLDWATCH_LCD_I2C_H

