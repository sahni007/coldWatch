/*
 * temperature_sensor.h
 * Sensor abstraction layer (pure C).
 *  - SRS1_001: reads temperature via ESP32 ADC (analog) or bit-banged
 *              OneWire digital protocol (DS18B20)
 *  - SRS1_005: targets +-1C accuracy (DS18B20 typ. +-0.5C in normal range)
 *  - SRS1_009: reports sensor type for LCD display
 *  - SRS1_010: detects sensor faults (disconnect/short) for Alarm 1010
 */
#ifndef COLDWATCH_TEMPERATURE_SENSOR_H
#define COLDWATCH_TEMPERATURE_SENSOR_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    SENSOR_STATE_OK = 0,
    SENSOR_STATE_FAULT = 1
} sensor_fault_state_t;

void temperature_sensor_init(void);

// Take one sample from the hardware. Returns true if 'out_temp_c' is valid
// (i.e. the sensor is not currently in a fault state).
bool temperature_sensor_sample(float *out_temp_c);

sensor_fault_state_t temperature_sensor_get_fault_state(void);
const char *temperature_sensor_get_type_name(void);

// SRS1_004: round a raw temperature to the configured resolution step
// (e.g. resolution=0.1 -> value snapped to nearest 0.1C).
float temperature_round_to_resolution(float value, float resolution);

#endif // COLDWATCH_TEMPERATURE_SENSOR_H

