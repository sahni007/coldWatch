/*
 * temperature_sensor.c
 */
#include "temperature_sensor.h"
#include "config.h"
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"

#if ACTIVE_SENSOR_TYPE == SENSOR_TYPE_DS18B20
  #include "onewire.h"
#endif

// DS18B20 ROM commands
#define DS18B20_CMD_SKIP_ROM       0xCC
#define DS18B20_CMD_CONVERT_T      0x44
#define DS18B20_CMD_READ_SCRATCHPAD 0xBE

static sensor_fault_state_t s_fault_state = SENSOR_STATE_OK;
static uint8_t s_bad_read_streak = 0;
static uint8_t s_good_read_streak = 0;
#if ACTIVE_SENSOR_TYPE != SENSOR_TYPE_DS18B20
static adc_oneshot_unit_handle_t s_adc_handle;
#endif

void temperature_sensor_init(void) {
#if ACTIVE_SENSOR_TYPE == SENSOR_TYPE_DS18B20
    onewire_init(ONE_WIRE_BUS_GPIO);
#else
    const adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    const adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_config, &s_adc_handle));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle,
                                                ANALOG_TEMP_ADC_CHAN,
                                                &channel_config));
#endif
}

const char *temperature_sensor_get_type_name(void) {
#if ACTIVE_SENSOR_TYPE == SENSOR_TYPE_DS18B20
    return "DS18B20";
#elif ACTIVE_SENSOR_TYPE == SENSOR_TYPE_ANALOG_LM35
    return "LM35";
#elif ACTIVE_SENSOR_TYPE == SENSOR_TYPE_ANALOG_NTC
    return "NTC";
#else
    return "UNKNOWN";
#endif
}

sensor_fault_state_t temperature_sensor_get_fault_state(void) {
    return s_fault_state;
}

// -------------------- hardware-specific reads --------------------

#if ACTIVE_SENSOR_TYPE == SENSOR_TYPE_DS18B20
static bool read_ds18b20(float *temp_c) {
    if (!onewire_reset(ONE_WIRE_BUS_GPIO)) {
        return false; // SRS1_010: no presence pulse -> disconnected
    }
    onewire_write_byte(ONE_WIRE_BUS_GPIO, DS18B20_CMD_SKIP_ROM);
    onewire_write_byte(ONE_WIRE_BUS_GPIO, DS18B20_CMD_CONVERT_T);

    // Wait for conversion (12-bit resolution needs up to 750ms).
    // DS18B20 holds the bus low while converting and releases it (reads
    // as '1') once the conversion is complete - poll for that, with an
    // overall timeout as a safety net.
    for (int i = 0; i < 800; i++) {
        if (onewire_read_bit(ONE_WIRE_BUS_GPIO)) break;
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    if (!onewire_reset(ONE_WIRE_BUS_GPIO)) {
        return false;
    }
    onewire_write_byte(ONE_WIRE_BUS_GPIO, DS18B20_CMD_SKIP_ROM);
    onewire_write_byte(ONE_WIRE_BUS_GPIO, DS18B20_CMD_READ_SCRATCHPAD);

    uint8_t scratchpad[9];
    for (int i = 0; i < 9; i++) {
        scratchpad[i] = onewire_read_byte(ONE_WIRE_BUS_GPIO);
    }

    // SRS1_010: validate CRC - corrupted read likely means a wiring/noise fault
    if (onewire_crc8(scratchpad, 8) != scratchpad[8]) {
        return false;
    }

    int16_t raw = (int16_t)((scratchpad[1] << 8) | scratchpad[0]);
    float t = raw / 16.0f; // 12-bit resolution -> 1 LSB = 1/16 C

    if (t == DS18B20_DISCONNECT_VALUE || t == DS18B20_POWERON_RESET_VALUE) {
        return false; // SRS1_010
    }
    *temp_c = t;
    return true;
}
#endif

#if ACTIVE_SENSOR_TYPE == SENSOR_TYPE_ANALOG_LM35
static bool read_analog_lm35(float *temp_c) {
    int raw;
    if (adc_oneshot_read(s_adc_handle, ANALOG_TEMP_ADC_CHAN, &raw) != ESP_OK) {
        return false;
    }
    // SRS1_010: open/short circuit detection via ADC rail-clamping
    if (raw <= ANALOG_OPEN_LOW_RAW || raw >= ANALOG_OPEN_HIGH_RAW) {
        return false;
    }
    float voltage = (raw * 3.9f) / 4095.0f; // ~0-3.9V full-scale at ADC_ATTEN_DB_12
    *temp_c = voltage * 100.0f;             // LM35: 10mV per degree C
    return true;
}
#endif

#if ACTIVE_SENSOR_TYPE == SENSOR_TYPE_ANALOG_NTC
static bool read_analog_ntc(float *temp_c) {
    int raw;
    if (adc_oneshot_read(s_adc_handle, ANALOG_TEMP_ADC_CHAN, &raw) != ESP_OK) {
        return false;
    }
    if (raw <= ANALOG_OPEN_LOW_RAW || raw >= ANALOG_OPEN_HIGH_RAW) {
        return false;
    }
    const float SERIES_R = 10000.0f;
    const float NOMINAL_R = 10000.0f;
    const float NOMINAL_T = 298.15f; // 25C in Kelvin
    const float BETA = 3950.0f;

    float resistance = SERIES_R * (4095.0f / (float)raw - 1.0f);
    float steinhart = logf(resistance / NOMINAL_R) / BETA;
    steinhart += 1.0f / NOMINAL_T;
    float kelvin = 1.0f / steinhart;
    *temp_c = kelvin - 273.15f;
    return true;
}
#endif

static bool read_raw(float *raw_temp_c) {
#if ACTIVE_SENSOR_TYPE == SENSOR_TYPE_DS18B20
    return read_ds18b20(raw_temp_c);
#elif ACTIVE_SENSOR_TYPE == SENSOR_TYPE_ANALOG_LM35
    return read_analog_lm35(raw_temp_c);
#elif ACTIVE_SENSOR_TYPE == SENSOR_TYPE_ANALOG_NTC
    return read_analog_ntc(raw_temp_c);
#else
    return false;
#endif
}

// -------------------- public sample() --------------------

bool temperature_sensor_sample(float *out_temp_c) {
    float raw;
    bool ok = read_raw(&raw);

    if (ok) {
        s_good_read_streak++;
        s_bad_read_streak = 0;
        if (s_fault_state == SENSOR_STATE_FAULT && s_good_read_streak >= SENSOR_RECOVER_CONSEC_READS) {
            s_fault_state = SENSOR_STATE_OK; // SRS1_011: clear fault once connection is fixed
        }
        *out_temp_c = raw;
        return (s_fault_state == SENSOR_STATE_OK);
    } else {
        s_bad_read_streak++;
        s_good_read_streak = 0;
        if (s_bad_read_streak >= SENSOR_FAULT_CONSEC_READS) {
            s_fault_state = SENSOR_STATE_FAULT; // SRS1_010
        }
        *out_temp_c = NAN;
        return false;
    }
}

float temperature_round_to_resolution(float value, float resolution) {
    if (resolution <= 0.0f || isnan(value)) return value;
    return roundf(value / resolution) * resolution;
}



