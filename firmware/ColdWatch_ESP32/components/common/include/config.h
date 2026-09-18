/*
 * config.h
 * Central configuration for ColdWatch firmware (ESP32 / ESP-IDF, pure C).
 * Maps to: SRS1_001..SRS1_011, SRS2_001..SRS2_011
 */
#ifndef COLDWATCH_CONFIG_H
#define COLDWATCH_CONFIG_H

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"

// ============================================================
// SRS1_009: Sensor type support (multiple sensor types on LCD)
// Plain integer macros so they can be used in #if preprocessor checks.
// ============================================================
#define SENSOR_TYPE_DS18B20     0   // OneWire digital sensor, typ. +-0.5C accuracy
#define SENSOR_TYPE_ANALOG_LM35 1   // Analog LM35, 10mV/C, via ESP32 ADC (SRS1_001)
#define SENSOR_TYPE_ANALOG_NTC  2   // Analog NTC thermistor via ESP32 ADC (SRS1_001)

// Select the physically connected sensor here (compile-time selection).
// DS18B20 is recommended to meet the +-1C accuracy target (SRS1_005).
#define ACTIVE_SENSOR_TYPE   SENSOR_TYPE_DS18B20

// ============================================================
// Pin assignments (ESP32 DevKitC style boards)
// ============================================================
#define ONE_WIRE_BUS_GPIO     GPIO_NUM_4     // DS18B20 data pin (needs 4.7k pull-up to 3.3V)
#define ANALOG_TEMP_ADC_CHAN  ADC_CHANNEL_6  // GPIO34 - analog sensor (LM35/NTC) - SRS1_001
#define BUZZER_GPIO           GPIO_NUM_25    // Buzzer, driven via LEDC PWM
#define ACK_BUTTON_GPIO       GPIO_NUM_27    // Push button (to GND, internal pull-up) - silence buzzer

// ============================================================
// Mains power-supply detection (GPIO34 ADC input).
// Wiring assumption (documented, since exact wiring wasn't specified):
// a mains-derived 3.3V rail is fed into GPIO34 (through a divider if it
// isn't already 0-3.3V safe). While mains power is present this reads
// ~3.3V; once the device falls back to its internal battery, this net
// collapses to ~0V. See components/power_source.
// NOTE: shares GPIO34 / ADC_CHANNEL_6 with the optional analog temperature
// sensor (ANALOG_TEMP_ADC_CHAN) above - only use one of the two on GPIO34
// at a time (default ACTIVE_SENSOR_TYPE is DS18B20, so GPIO34 is free).
// ============================================================
#define POWER_SOURCE_ADC_CHAN                ADC_CHANNEL_6  // GPIO34
#define POWER_SOURCE_GPIO                    GPIO_NUM_34
#define POWER_SOURCE_MAIN_VOLTAGE_THRESHOLD  2.0f    // volts; >= this => mains power present
#define POWER_SOURCE_DEBOUNCE_WINDOW_MS       3000   // must be stable this long before switching state
#define POWER_SOURCE_SAMPLE_INTERVAL_MS        200   // how often main.c samples the power source

// ============================================================
// SRS3_004/005/006: Internal battery sensing strategy.
//
// Pick ACTIVE_BATTERY_SENSE_MODE to match your ACTUAL wiring:
//
//  - BATTERY_SENSE_MODE_DIRECT (default/recommended, most accurate):
//      A dedicated analog wire is tapped directly off the battery's + lead
//      (BEFORE any boost/buck regulator or power-path IC), through a
//      resistor divider, into BATTERY_ADC_CHAN. Gives a real 0-100% state
//      of charge. Requires ONE extra wire beyond the shared power+GND pair.
//
//  - BATTERY_SENSE_MODE_DIGITAL_STATUS:
//      No analog tap available, but the battery/boost/charger module
//      already exposes a single digital "low battery" status pin (often
//      labelled LBO/PB, or the pin driving a "LOW BATT" LED on cheap boost
//      modules). Only gives a binary low-battery flag, no percentage - but
//      needs just ONE extra digital wire, which most such modules already
//      break out on a header, so it's usually trivial to add.
//
//  - BATTERY_SENSE_MODE_RAIL_SAG (last resort - use only if you truly
//      cannot add ANY extra wire, i.e. the board only ever receives the
//      shared, already-regulated rail + GND from a switchover circuit that
//      feeds from EITHER the mains adapter OR the battery's boost
//      converter): the battery's real voltage is intentionally hidden by
//      that regulator, so an accurate percentage is NOT physically
//      possible from these 2 wires alone. This mode instead watches the
//      shared rail itself (via a divider into BATTERY_ADC_CHAN) for the
//      brief undervoltage "sag" most cheap boost converters show right
//      before they lose regulation - i.e. you get a late, coarse
//      "critical battery" warning seconds before shutdown, NOT a gradual
//      100%->0% curve. Percentage in this mode is only meaningful near 0%.
// ============================================================
#define BATTERY_SENSE_MODE_DIRECT          0
#define BATTERY_SENSE_MODE_DIGITAL_STATUS  1
#define BATTERY_SENSE_MODE_RAIL_SAG        2

#define ACTIVE_BATTERY_SENSE_MODE   BATTERY_SENSE_MODE_DIRECT

// ---- BATTERY_SENSE_MODE_DIRECT wiring ----
// The battery's + terminal is fed into GPIO35 (ADC1 channel 7) through a
// 2:1 resistor divider (e.g. two 100k resistors), so a single Li-ion
// cell's 3.0-4.2V range appears as a safe 1.5-2.1V at the ADC pin.
#define BATTERY_ADC_CHAN                 ADC_CHANNEL_7   // GPIO35
#define BATTERY_GPIO                     GPIO_NUM_35
#define BATTERY_VOLTAGE_DIVIDER_RATIO    2.0f    // ADC pin voltage x this = actual battery voltage
#define BATTERY_VOLTAGE_MAX              4.2f    // volts -> 100% (single-cell Li-ion, fully charged)
#define BATTERY_VOLTAGE_MIN              3.0f    // volts -> 0%   (single-cell Li-ion, discharge cutoff)

// ---- BATTERY_SENSE_MODE_DIGITAL_STATUS wiring ----
// Wire the module's low-battery status pin (LBO/PB/etc.) straight into a
// spare GPIO. Most of these outputs are open-drain, active-LOW when the
// battery is low, so we'd normally enable the internal pull-up - BUT
// GPIO34-39 on the original ESP32 are input-only and have NO internal
// pull-up/pull-down resistors. If you keep BATTERY_STATUS_GPIO on GPIO35,
// add an external 10k pull-up to 3.3V on that line. If you'd rather rely
// on the internal pull-up, change BATTERY_STATUS_GPIO to a normal
// bidirectional GPIO instead (e.g. GPIO26 or GPIO33, if free on your board).
#define BATTERY_STATUS_GPIO              GPIO_NUM_35     // spare digital-capable GPIO (needs external pull-up - see above)
#define BATTERY_STATUS_ACTIVE_LOW        1               // 1 = pin reads LOW when battery is low

// ---- BATTERY_SENSE_MODE_RAIL_SAG wiring ----
// Reuses BATTERY_ADC_CHAN/BATTERY_GPIO above, but the divider is sized for
// the shared rail's nominal voltage (e.g. 5V), not the raw battery. Example:
// a 100k/56k divider on a 5V rail -> ~1.8V at the ADC pin (ratio ~2.79).
#define RAIL_SAG_DIVIDER_RATIO           2.79f
#define RAIL_NOMINAL_VOLTAGE             5.0f    // volts; treated as "100%" (rail healthy)
#define RAIL_SAG_CRITICAL_VOLTAGE        4.5f    // volts; treated as "0%" (converter about to drop out)

#define BATTERY_LOW_PERCENT              20      // SRS3_005: at/below this -> low-battery alarm
#define BATTERY_CRITICAL_PERCENT          5      // SRS3_006: at/below this -> critical, snapshot + save
#define BATTERY_LOW_HYSTERESIS_CNT        5      // consecutive samples to confirm/clear low-battery
#define BATTERY_CRITICAL_HYSTERESIS_CNT   5      // consecutive samples to confirm/clear critical-battery
#define ALARM_ID_POWER_SOURCE   3001   // SRS3_001/003 (main power lost/restored)
#define ALARM_ID_LOW_BATTERY    3005   // SRS3_005
#define ALARM_ID_CRITICAL_BATTERY 3006 // SRS3_006
// DHT11 temperature/humidity sensor (optional, independent of the main
// cold-chain sensor above). Needs a 4.7k-10k pull-up to 3.3V on the data
// line (most DHT11 breakout boards already include one on-board).
// SRS2_001: humidity is read via the DHT11's proprietary single-wire
// protocol (bit-banged GPIO) - documented assumption, since the requirement
// text left the protocol TBD ("GPIO or any other protocol").
#define ENABLE_DHT11          1
#define DHT11_GPIO            GPIO_NUM_18
#define DHT11_SAMPLE_INTERVAL_MS  2000   // DHT11 max sample rate is ~1Hz

// SRS2_009: only one humidity sensor type is currently implemented (DHT11),
// but the getter function pattern mirrors SRS1_009 so more types could be
// added later the same way ACTIVE_SENSOR_TYPE works for temperature.

// ILI9341 TFT display over SPI. These pins avoid the sensor and modem pins.
#define LCD_SPI_SCLK_GPIO       GPIO_NUM_14
#define LCD_SPI_MOSI_GPIO       GPIO_NUM_13
#define LCD_SPI_MISO_GPIO       GPIO_NUM_12
#define LCD_SPI_CS_GPIO         GPIO_NUM_15
#define LCD_SPI_DC_GPIO         GPIO_NUM_2
#define LCD_SPI_RESET_GPIO      (-1)  // TFT RESET is connected to ESP32 EN
#define LCD_SPI_BACKLIGHT_GPIO  GPIO_NUM_21
#define LCD_WIDTH               240
#define LCD_HEIGHT              320

// UART for SIM800L / SIM900 GSM module
#define GSM_UART_PORT         UART_NUM_2
#define GSM_UART_TX_GPIO      GPIO_NUM_17
#define GSM_UART_RX_GPIO      GPIO_NUM_16
#define GSM_BAUD_RATE         9600
#define SMS_SEND_TIMEOUT_MS   5000

// ============================================================
// SRS1_002 / SRS1_003 / SRS1_004 / SRS1_008: default configurable properties
// Persisted at runtime in ESP32 NVS (see nvs_storage.c) and changeable via
// the serial console (see main.c "SET ..." commands).
// ============================================================
    #define DEFAULT_TEMP_HIGH_LIMIT      30.0f   // SRS1_002
#define DEFAULT_TEMP_LOW_LIMIT        0.0f   // SRS1_003
#define DEFAULT_TEMP_RESOLUTION       0.1f   // SRS1_004 (deg C per step)

// SRS1_008: temperatureHysteresis = 30
// Interpretation: number of CONSECUTIVE samples for which the temperature
// must remain beyond (or back within) a limit before the firmware accepts
// the change as real (debounce / anti-chatter filter), i.e. a "count" of
// samples, not a temperature delta. At a 1s sample rate this is a 30s
// confirmation window. (Documented assumption - spec text is ambiguous.)
#define DEFAULT_TEMP_HYSTERESIS_CNT   3

// ============================================================
// SRS2_002 / SRS2_003 / SRS2_004 / SRS2_008: humidity configurable properties
// Persisted at runtime in ESP32 NVS (see nvs_storage.c) and changeable via
// the serial console (see main.c "SET HUM..." commands).
// ============================================================
#define DEFAULT_HUMIDITY_HIGH_LIMIT   100.0f  // SRS2_002
#define DEFAULT_HUMIDITY_LOW_LIMIT      0.0f  // SRS2_003
#define DEFAULT_HUMIDITY_RESOLUTION      0.1f // SRS2_004 (%RH per step)

// SRS2_008: humidityHysteresis = 30 (consecutive-sample debounce count,
// same interpretation/assumption as SRS1_008 for temperature).
#define DEFAULT_HUMIDITY_HYSTERESIS_CNT 30

// ============================================================
// Alarm IDs
// ============================================================
#define ALARM_ID_HIGH_TEMP     1006   // SRS1_006
#define ALARM_ID_LOW_TEMP      1007   // SRS1_007
#define ALARM_ID_SENSOR_FAULT  1010   // SRS1_010
#define ALARM_ID_HIGH_HUMIDITY        2006   // SRS2_006
#define ALARM_ID_LOW_HUMIDITY         2007   // SRS2_007
#define ALARM_ID_HUMIDITY_SENSOR_FAULT 2010  // SRS2_010

// ============================================================
// Buzzer (SRS1_006 / SRS1_007: "activate buzzer at a defined frequency")
// ============================================================
#define BUZZER_LEDC_TIMER       LEDC_TIMER_0
#define BUZZER_LEDC_CHANNEL     LEDC_CHANNEL_0
#define BUZZER_LEDC_MODE        LEDC_LOW_SPEED_MODE
#define BUZZER_LEDC_DUTY_RES    LEDC_TIMER_8_BIT   // 0-255 duty range
#define BUZZER_LEDC_DUTY_ON     128                // 50% duty = audible tone

#define BUZZER_FREQ_HIGH_ALARM_HZ   2500
#define BUZZER_FREQ_LOW_ALARM_HZ    2000
#define BUZZER_FREQ_FAULT_ALARM_HZ  1500
#define BUZZER_BEEP_ON_MS            300
#define BUZZER_BEEP_OFF_MS           700

// SRS2_006 / SRS2_007: distinct tones for humidity alarms so they can be
// told apart from temperature alarms by ear.
#define BUZZER_FREQ_HIGH_HUMIDITY_ALARM_HZ   2700
#define BUZZER_FREQ_LOW_HUMIDITY_ALARM_HZ    2200
#define BUZZER_FREQ_HUMIDITY_FAULT_ALARM_HZ  1700

// Power-source alarm tone (distinct from the temperature/humidity tones).
#define BUZZER_FREQ_POWER_ALARM_HZ           1900

// SRS3_005/006: battery alarm tones (distinct from all of the above).
#define BUZZER_FREQ_LOW_BATTERY_ALARM_HZ      1600
#define BUZZER_FREQ_CRITICAL_BATTERY_ALARM_HZ 1300

// ============================================================
// SRS1_010: Sensor fault detection thresholds
// ============================================================
#define SENSOR_FAULT_CONSEC_READS     5     // consecutive bad reads -> declare fault
#define SENSOR_RECOVER_CONSEC_READS   5     // consecutive good reads -> clear fault (SRS1_011)
#define DS18B20_DISCONNECT_VALUE   -127.0f  // no presence pulse / bad CRC sentinel
#define DS18B20_POWERON_RESET_VALUE 85.0f   // DS18B20 power-on-reset garbage value
#define ANALOG_OPEN_LOW_RAW           20    // raw ADC (0-4095, 12-bit) below this => broken/open wire
#define ANALOG_OPEN_HIGH_RAW        4075    // raw ADC above this => shorted/open wire

// ============================================================
// SRS2_010: Humidity (DHT11) sensor fault detection.
// Documented assumption: a "connection problem" is detected the same way
// as the DS18B20 (SRS1_010) - i.e. the sensor fails to respond to the
// handshake, or its checksum doesn't validate (electrical noise / bad
// wiring), confirmed over several consecutive reads to avoid false
// positives from a single missed 1-second timing window.
// ============================================================
#define HUMIDITY_FAULT_CONSEC_READS    5    // consecutive bad reads -> declare fault (SRS2_010)
#define HUMIDITY_RECOVER_CONSEC_READS  5    // consecutive good reads -> clear fault (SRS2_011)

// ============================================================
// SMS destination
// ============================================================
#define SMS_DESTINATION_NUMBER  "+10000000000"   // TODO: set the real destination number

// ============================================================
// Sampling
// ============================================================
#define SENSOR_SAMPLE_INTERVAL_MS   1000
#define LCD_REFRESH_INTERVAL_MS      500
#define BUZZER_UPDATE_INTERVAL_MS     50

#endif // COLDWATCH_CONFIG_H

