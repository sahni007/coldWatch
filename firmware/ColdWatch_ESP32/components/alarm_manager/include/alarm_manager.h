/*
 * alarm_manager.h
 * Implements SRS1_006, SRS1_007, SRS1_008, SRS1_010, SRS1_011 (temperature)
 * and SRS2_006, SRS2_007, SRS2_008, SRS2_010, SRS2_011 (humidity).
 *
 * Debounce/hysteresis strategy (SRS1_008 / SRS2_008):
 *  An alarm condition (or its clearing) must be observed for
 *  'temperatureHysteresis' / 'humidityHysteresis' CONSECUTIVE samples
 *  before the firmware acts on it. This filters noise/transient spikes.
 *
 * Auto-clear strategy (SRS1_011 / SRS2_011):
 *  Alarm 1007 (low temp), 1010 (temp sensor fault), 2007 (low humidity) and
 *  2010 (humidity sensor fault) are cleared automatically by firmware once
 *  the condition is resolved for the debounce window. Alarms 1006 and 2006
 *  (high temp / high humidity) are also auto-cleared using the same
 *  hysteresis window for consistency; set ENABLE_HIGH_AUTO_CLEAR to 0
 *  below if your process requires a manual acknowledgement instead.
 *  (Note: SRS2_011's text says "clear the alarm 1006 & 2010" - taken as a
 *  literal reference to the existing SRS1_011 auto-clear behavior plus
 *  the new 2010 humidity fault alarm; 2006/2007 are auto-cleared the same
 *  way for consistency.)
 */
#ifndef COLDWATCH_ALARM_MANAGER_H
#define COLDWATCH_ALARM_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

#define ENABLE_HIGH_AUTO_CLEAR 1

void alarm_manager_init(void);

// Call once per temperature sample cycle.
void alarm_manager_update(float temperature, bool temp_valid, bool sensor_fault);

// Call once per humidity sample cycle (SRS2_006/007/008/010/011).
void alarm_manager_update_humidity(float humidity, bool humidity_valid, bool sensor_fault);

// Drives the LCD / buzzer according to current alarm priority.
// Call periodically (independent of the sample rate).
void alarm_manager_refresh_outputs(const char *sensor_type_name, float last_temperature, bool last_temp_valid,
                                    const char *humidity_sensor_type_name, float last_humidity, bool last_humidity_valid);

bool alarm_manager_is_high_active(void);
bool alarm_manager_is_low_active(void);
bool alarm_manager_is_fault_active(void);

bool alarm_manager_is_humidity_high_active(void);
bool alarm_manager_is_humidity_low_active(void);
bool alarm_manager_is_humidity_fault_active(void);

#endif // COLDWATCH_ALARM_MANAGER_H

