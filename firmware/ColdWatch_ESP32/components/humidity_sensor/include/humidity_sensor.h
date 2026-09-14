/*
 * humidity_sensor.h
 * Humidity sensor abstraction layer (pure C), mirrors temperature_sensor.h.
 *  - SRS2_001: reads humidity via the DHT11 (bit-banged single-wire, see
 *              components/dht11)
 *  - SRS2_005: targets +-3RH accuracy (DHT11 datasheet typ. +-5RH, worst
 *              case for cheap breakout modules - documented assumption /
 *              limitation, see README)
 *  - SRS2_009: reports sensor type for LCD display
 *  - SRS2_010: detects sensor faults (disconnect/checksum) for Alarm 2010
 */
#ifndef COLDWATCH_HUMIDITY_SENSOR_H
#define COLDWATCH_HUMIDITY_SENSOR_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    HUMIDITY_SENSOR_STATE_OK = 0,
    HUMIDITY_SENSOR_STATE_FAULT = 1
} humidity_sensor_fault_state_t;

void humidity_sensor_init(void);

// Take one sample from the hardware. Returns true if both output values are
// valid (i.e. the sensor is not currently in a fault state).
bool humidity_sensor_sample(float *out_temperature_c, float *out_humidity_pct);

humidity_sensor_fault_state_t humidity_sensor_get_fault_state(void);
const char *humidity_sensor_get_type_name(void); // SRS2_009

// SRS2_004: round a raw humidity reading to the configured resolution step.
float humidity_round_to_resolution(float value, float resolution);

#endif // COLDWATCH_HUMIDITY_SENSOR_H

