/*
 * config.h
 * Central configuration for ColdWatch firmware (ESP32 / ESP-IDF, pure C).
 * Maps to: SRS1_001..SRS1_011, SRS2_001..SRS2_011
 */
#ifndef COLDWATCH_CONFIG_H
#define COLDWATCH_CONFIG_H

#include "driver/gpio.h"
#include "driver/adc.h"

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
#define ANALOG_TEMP_ADC_CHAN  ADC1_CHANNEL_6 // GPIO34 - analog sensor (LM35/NTC) - SRS1_001
#define BUZZER_GPIO           GPIO_NUM_25    // Buzzer, driven via LEDC PWM
#define ACK_BUTTON_GPIO       GPIO_NUM_27    // Push button (to GND, internal pull-up) - silence buzzer

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

// I2C bus for LCD (PCF8574 HD44780 backpack)
#define I2C_MASTER_PORT       I2C_NUM_0
#define I2C_MASTER_SDA_GPIO   GPIO_NUM_21
#define I2C_MASTER_SCL_GPIO   GPIO_NUM_22
#define I2C_MASTER_FREQ_HZ    100000
#define LCD_I2C_ADDR          0x27
#define LCD_COLUMNS           16
#define LCD_ROWS              2

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
#define DEFAULT_TEMP_HIGH_LIMIT      50.0f   // SRS1_002
#define DEFAULT_TEMP_LOW_LIMIT        0.0f   // SRS1_003
#define DEFAULT_TEMP_RESOLUTION       0.1f   // SRS1_004 (deg C per step)

// SRS1_008: temperatureHysteresis = 30
// Interpretation: number of CONSECUTIVE samples for which the temperature
// must remain beyond (or back within) a limit before the firmware accepts
// the change as real (debounce / anti-chatter filter), i.e. a "count" of
// samples, not a temperature delta. At a 1s sample rate this is a 30s
// confirmation window. (Documented assumption - spec text is ambiguous.)
#define DEFAULT_TEMP_HYSTERESIS_CNT   30

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

