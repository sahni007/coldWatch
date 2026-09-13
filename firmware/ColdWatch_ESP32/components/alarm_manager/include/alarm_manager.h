/*
 * alarm_manager.h
 * Implements SRS1_006, SRS1_007, SRS1_008, SRS1_010, SRS1_011.
 *
 * Debounce/hysteresis strategy (SRS1_008):
 *  An alarm condition (or its clearing) must be observed for
 *  'temperatureHysteresis' CONSECUTIVE samples before the firmware
 *  acts on it. This filters noise/transient spikes.
 *
 * Auto-clear strategy (SRS1_011):
 *  Alarm 1007 (low temp) and Alarm 1010 (sensor fault) are cleared
 *  automatically by firmware once the condition is resolved for the
 *  debounce window. Alarm 1006 (high temp) is also auto-cleared using
 *  the same hysteresis window for consistency; set ENABLE_HIGH_AUTO_CLEAR
 *  to 0 below if your process requires a manual acknowledgement instead.
 */
#ifndef COLDWATCH_ALARM_MANAGER_H
#define COLDWATCH_ALARM_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

#define ENABLE_HIGH_AUTO_CLEAR 1

void alarm_manager_init(void);

// Call once per sensor sample cycle.
void alarm_manager_update(float temperature, bool temp_valid, bool sensor_fault);

// Drives the LCD / buzzer according to current alarm priority.
// Call periodically (independent of the sample rate).
void alarm_manager_refresh_outputs(const char *sensor_type_name, float last_temperature, bool last_temp_valid);

bool alarm_manager_is_high_active(void);
bool alarm_manager_is_low_active(void);
bool alarm_manager_is_fault_active(void);

#endif // COLDWATCH_ALARM_MANAGER_H

