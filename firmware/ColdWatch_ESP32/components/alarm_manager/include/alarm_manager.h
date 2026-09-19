/*
 * alarm_manager.h
 * Implements SRS1_006, SRS1_007, SRS1_008, SRS1_010, SRS1_011 (temperature),
 * SRS2_006, SRS2_007, SRS2_008, SRS2_010, SRS2_011 (humidity), and
 * SRS3_001, SRS3_003, SRS3_005, SRS3_006 (power/battery).
 *
 * Debounce/hysteresis strategy (SRS1_008 / SRS2_008):
 *  An alarm condition (or its clearing) must be observed for
 *  'temperatureHysteresis' / 'humidityHysteresis' CONSECUTIVE samples
 *  before the firmware acts on it. This filters noise/transient spikes.
 *  The battery low/critical alarms (SRS3_005/006) use the same
 *  consecutive-sample strategy via BATTERY_LOW_HYSTERESIS_CNT /
 *  BATTERY_CRITICAL_HYSTERESIS_CNT (config.h).
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
 *  Alarm 3001 (power lost), 3005 (low battery) and 3006 (critical battery)
 *  are likewise auto-cleared once the debounced condition resolves.
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

// Call once per power-source sample cycle. 'on_battery' should already be
// debounced (see components/power_source, power_source_update()). Raises
// ALARM_ID_POWER_SOURCE when mains power is lost (SRS3_001), clears it
// when restored (SRS3_003).
void alarm_manager_update_power(bool on_battery);

// Call once per power-source sample cycle (SRS3_004/005/006). 'voltage'
// and 'percent' come from power_source_get_battery_voltage()/percentage().
// 'on_battery' and the last known temperature/humidity readings are only
// used to fill in the SRS3_006 emergency snapshot if/when the critical
// threshold is reached.
void alarm_manager_update_battery(float voltage, uint8_t percent, bool on_battery,
                                   float last_temperature, bool last_temp_valid,
                                   float last_humidity, bool last_humidity_valid);

// Drives the LCD / buzzer according to current alarm priority. Call
// periodically (independent of the sample rate). Returns true if an alarm
// is active (the full-status alarm screen was drawn - caller should skip
// drawing its own selected screen this cycle), false if no alarm is active
// (nothing was drawn - caller should render whichever of the 4 navigable
// screens is currently selected, e.g. via lcd_show_home()/_alarms_screen()/
// _power_screen()/_gsm_screen()).
bool alarm_manager_refresh_outputs(const char *sensor_type_name, float last_temperature, bool last_temp_valid,
                                    const char *humidity_sensor_type_name, float last_humidity, bool last_humidity_valid,
                                    const char *power_source_name, float battery_voltage, uint8_t battery_percentage);

bool alarm_manager_is_high_active(void);
bool alarm_manager_is_low_active(void);
bool alarm_manager_is_fault_active(void);

bool alarm_manager_is_humidity_high_active(void);
bool alarm_manager_is_humidity_low_active(void);
bool alarm_manager_is_humidity_fault_active(void);

bool alarm_manager_is_power_lost_active(void);
bool alarm_manager_is_battery_low_active(void);
bool alarm_manager_is_battery_critical_active(void);

// ---- ALARMS screen support (multi-screen touch UI) ----
// How many of the 10 possible alarms are currently RAISED.
uint8_t alarm_manager_get_active_count(void);

// Fills out_ids[]/out_names[] (both arrays must have room for at least
// max_count entries) with the currently active alarms, most-severe-first
// (same priority order as alarm_manager_refresh_outputs()). Returns how
// many entries were actually filled (0..max_count).
uint8_t alarm_manager_get_active_list(uint16_t *out_ids, const char **out_names, uint8_t max_count);

#endif // COLDWATCH_ALARM_MANAGER_H

