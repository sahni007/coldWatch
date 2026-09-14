/*
 * humidity_sensor.c
 * Wraps the raw DHT11 bit-banged driver (components/dht11) with a
 * consecutive-read fault-debounce state machine, exactly the same pattern
 * temperature_sensor.c uses on top of onewire.c for the DS18B20.
 */
#include "humidity_sensor.h"
#include "config.h"
#include "dht11.h"
#include <math.h>

static humidity_sensor_fault_state_t s_fault_state = HUMIDITY_SENSOR_STATE_OK;
static uint8_t s_bad_read_streak = 0;
static uint8_t s_good_read_streak = 0;

void humidity_sensor_init(void) {
#if ENABLE_DHT11
    dht11_init(DHT11_GPIO);
#endif
}

const char *humidity_sensor_get_type_name(void) {
#if ENABLE_DHT11
    return "DHT11"; // SRS2_009
#else
    return "UNKNOWN";
#endif
}

humidity_sensor_fault_state_t humidity_sensor_get_fault_state(void) {
    return s_fault_state;
}

bool humidity_sensor_sample(float *out_humidity_pct) {
#if ENABLE_DHT11
    float temp_c_unused, humidity;
    bool ok = dht11_read(&temp_c_unused, &humidity); // SRS2_001
#else
    float humidity = NAN;
    bool ok = false;
#endif

    if (ok) {
        s_good_read_streak++;
        s_bad_read_streak = 0;
        if (s_fault_state == HUMIDITY_SENSOR_STATE_FAULT &&
            s_good_read_streak >= HUMIDITY_RECOVER_CONSEC_READS) {
            s_fault_state = HUMIDITY_SENSOR_STATE_OK; // SRS2_011: clear fault once connection is fixed
        }
        *out_humidity_pct = humidity;
        return (s_fault_state == HUMIDITY_SENSOR_STATE_OK);
    } else {
        s_bad_read_streak++;
        s_good_read_streak = 0;
        if (s_bad_read_streak >= HUMIDITY_FAULT_CONSEC_READS) {
            s_fault_state = HUMIDITY_SENSOR_STATE_FAULT; // SRS2_010
        }
        *out_humidity_pct = NAN;
        return false;
    }
}

float humidity_round_to_resolution(float value, float resolution) {
    if (resolution <= 0.0f || isnan(value)) return value;
    return roundf(value / resolution) * resolution;
}

