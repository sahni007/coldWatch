/*
 * power_source.h
 * Mains power-supply presence detection (SRS3_001/003) and internal
 * battery voltage/percentage monitoring (SRS3_004/005/006).
 *
 * Requirement: detect whether the device is being fed from the mains
 * (main) power supply or has fallen back to its internal battery, and
 * track the battery's state of charge.
 *
 * Hardware assumption (documented, since the wiring wasn't specified):
 *  - A mains-derived 3.3V rail (from the charger/PSU) is connected to
 *    GPIO34 (ADC1 channel 6), optionally through a resistor divider if the
 *    rail is not already at a safe 0-3.3V level for the ESP32 ADC.
 *  - While mains power is present, GPIO34 reads ~3.3V.
 *  - When mains power is lost and the board switches to its internal
 *    battery, that rail collapses to ~0V, so GPIO34 reads ~0V.
 *  - Raw ADC counts are converted to real millivolts using ESP-IDF's ADC
 *    calibration API (line-fitting scheme on ESP32) when available, which
 *    is significantly more accurate than a plain raw/4095 linear
 *    approximation. Falls back to the linear approximation if calibration
 *    can't be created (e.g. required eFuse calibration bits aren't burnt).
 *
 * Battery sensing has THREE selectable modes (config.h ->
 * ACTIVE_BATTERY_SENSE_MODE), because how accurately you can measure the
 * battery depends entirely on what's physically wired:
 *  - BATTERY_SENSE_MODE_DIRECT (default): a dedicated wire taps the battery
 *    directly (before any boost/buck regulator), through a divider, into
 *    BATTERY_ADC_CHAN (GPIO35). Gives a real, accurate 0-100% reading.
 *  - BATTERY_SENSE_MODE_DIGITAL_STATUS: only a single digital "low battery"
 *    status pin is available from the battery/boost module (no analog
 *    tap). Gives a binary low-battery flag only (no percentage curve).
 *  - BATTERY_SENSE_MODE_RAIL_SAG: NO extra wire exists at all - the board
 *    only receives the shared, already-regulated rail (e.g. 5V) + GND,
 *    fed by EITHER the mains adapter OR the battery's boost converter.
 *    Since a regulator's whole job is to hide the battery's real voltage,
 *    an accurate percentage is NOT physically possible this way. This
 *    mode instead watches the shared rail for the undervoltage "sag" most
 *    cheap boost converters show right before losing regulation, giving a
 *    late, coarse "critical battery" warning only - NOT a real 100%->0%
 *    curve. Use power_source_battery_reading_is_accurate() to know which
 *    situation you're in at runtime (useful for LCD/log annotation).
 *
 * Debouncing:
 *  A raw ADC sample can be noisy or glitch momentarily during a real
 *  transition (e.g. relay/charger switch bounce). Instead of reacting to a
 *  single sample, this module requires the new reading to remain stable
 *  for POWER_SOURCE_DEBOUNCE_WINDOW_MS (see config.h) before the confirmed
 *  power source is updated. Call power_source_update() periodically (every
 *  POWER_SOURCE_SAMPLE_INTERVAL_MS) - it internally tracks time and only
 *  flips power_source_get_state() once the new reading has "won" for the
 *  full debounce window. The same call also refreshes the battery
 *  voltage/percentage (SRS3_004); low/critical debouncing for the battery
 *  is handled in alarm_manager (alarm_manager_update_battery()), mirroring
 *  the consecutive-sample hysteresis pattern used for temperature/humidity.
 */
#ifndef COLDWATCH_POWER_SOURCE_H
#define COLDWATCH_POWER_SOURCE_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    POWER_SOURCE_MAIN    = 0,  // GPIO34 ~3.3V -> mains power supply in use
    POWER_SOURCE_BATTERY = 1   // GPIO34 ~0V   -> running on internal battery
} power_source_state_t;

// Configure the ADC/GPIO channels used to sense the power-supply rail
// (GPIO34) and the battery (mode-dependent - see config.h).
void power_source_init(void);

// Take one sample of BOTH the mains-sense reading (debounced) and the
// battery reading (mode-dependent). Call this periodically (e.g. every
// POWER_SOURCE_SAMPLE_INTERVAL_MS) from the main loop. Returns true the
// moment the CONFIRMED power source state changes (i.e. once per debounced
// transition), false otherwise.
bool power_source_update(void);

// Current confirmed (debounced) power source.
power_source_state_t power_source_get_state(void);

// Convenience helper: true while running on mains power.
bool power_source_is_main(void);

// Convenience helper: true while running on the internal battery.
bool power_source_is_battery(void);

// Last raw voltage measured on GPIO34 (volts), for diagnostics/logging.
float power_source_get_last_voltage(void);

// "MAIN" or "BATTERY", for logging / LCD display.
const char *power_source_get_state_name(void);

// ---- Battery voltage/percentage (SRS3_004) ----

// Last measured battery (or, in RAIL_SAG mode, shared-rail) voltage in
// volts. In BATTERY_SENSE_MODE_DIGITAL_STATUS this is always 0.0 (no
// analog reading exists in that mode).
float power_source_get_battery_voltage(void);

// Battery state of charge, 0-100%. Meaning depends on
// ACTIVE_BATTERY_SENSE_MODE - see power_source_battery_reading_is_accurate().
uint8_t power_source_get_battery_percentage(void);

// True if power_source_get_battery_percentage() is a real, accurate state
// of charge (BATTERY_SENSE_MODE_DIRECT). False means the value is only a
// coarse proxy (RAIL_SAG: meaningful near 0% only) or a fixed placeholder
// derived from a binary status flag (DIGITAL_STATUS) - use this to decide
// whether to show "~87%" vs. just "OK"/"LOW" on the LCD, for example.
bool power_source_battery_reading_is_accurate(void);

#endif // COLDWATCH_POWER_SOURCE_H




