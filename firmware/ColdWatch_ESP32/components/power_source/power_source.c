/*
 * power_source.c
 */
#include "power_source.h"
#include "config.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_timer.h"
#include "esp_check.h"
#include "esp_log.h"
#include "driver/gpio.h"

static const char *TAG = "POWER_SOURCE";

static adc_oneshot_unit_handle_t s_adc_handle;

// ADC calibration handle, shared by both channels (GPIO34 mains-sense and
// GPIO35 battery-sense, when the latter is used in ADC mode): line-fitting
// calibration on ESP32 only depends on {unit, atten, bitwidth} - not on the
// channel - so one handle covers both. Converts raw ADC counts to real
// millivolts, correcting for the chip's inherent ADC non-linearity (a
// plain raw*3.9/4095 approximation can be off by several hundred mV).
static adc_cali_handle_t s_cali_handle = NULL;
static bool s_cali_available = false;

// Conservative default until the first sample arrives: assume battery so
// any code that reads the state before power_source_update() has run at
// least once doesn't wrongly assume mains power is present.
static power_source_state_t s_confirmed_state = POWER_SOURCE_BATTERY;
static power_source_state_t s_candidate_state  = POWER_SOURCE_BATTERY;
static int64_t s_candidate_since_ms = 0;
static float   s_last_voltage = 0.0f;
static bool    s_first_sample = true;

// Battery voltage/percentage (SRS3_004) - meaning depends on
// ACTIVE_BATTERY_SENSE_MODE, see power_source.h for the full explanation.
static float   s_battery_voltage = 0.0f;
static uint8_t s_battery_percentage = 0;

static inline int64_t millis64(void) {
    return esp_timer_get_time() / 1000;
}

static void init_calibration(void) {
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id  = ADC_UNIT_1,
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
#if CONFIG_IDF_TARGET_ESP32
        .default_vref = 1100, // mV; used only if eFuse Vref/TP calibration bits aren't burnt
#endif
    };
    esp_err_t err = adc_cali_create_scheme_line_fitting(&cali_config, &s_cali_handle);
    if (err == ESP_OK) {
        s_cali_available = true;
        ESP_LOGI(TAG, "ADC line-fitting calibration active (accurate mV readings)");
    } else {
        s_cali_available = false;
        ESP_LOGW(TAG, "ADC calibration unavailable (err=%s) - falling back to "
                      "uncalibrated linear approximation for voltage readings", esp_err_to_name(err));
    }
}

void power_source_init(void) {
    const adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id  = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    const adc_oneshot_chan_cfg_t channel_config = {
        .atten    = ADC_ATTEN_DB_12,   // ~0-3.9V full-scale, covers a 3.3V rail
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_config, &s_adc_handle));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle, POWER_SOURCE_ADC_CHAN, &channel_config));

#if ACTIVE_BATTERY_SENSE_MODE == BATTERY_SENSE_MODE_DIRECT || \
    ACTIVE_BATTERY_SENSE_MODE == BATTERY_SENSE_MODE_RAIL_SAG
    // Both modes read an analog voltage on BATTERY_ADC_CHAN (GPIO35),
    // sharing ADC1 with the mains-sense channel above (one oneshot handle
    // per physical ADC unit).
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle, BATTERY_ADC_CHAN, &channel_config));
#elif ACTIVE_BATTERY_SENSE_MODE == BATTERY_SENSE_MODE_DIGITAL_STATUS
    // No analog battery tap available - just a single digital "low
    // battery" status pin from the battery/boost module.
    gpio_reset_pin(BATTERY_STATUS_GPIO);
    gpio_set_direction(BATTERY_STATUS_GPIO, GPIO_MODE_INPUT);
#if BATTERY_STATUS_ACTIVE_LOW
    gpio_set_pull_mode(BATTERY_STATUS_GPIO, GPIO_PULLUP_ONLY);
#else
    gpio_set_pull_mode(BATTERY_STATUS_GPIO, GPIO_PULLDOWN_ONLY);
#endif
#endif

    init_calibration();
}

// Returns the measured voltage on the given channel (volts), or a negative
// value on ADC read failure. Uses calibrated mV conversion when available
// (much more accurate than a plain raw/4095 linear approximation), and
// falls back to the linear approximation if calibration couldn't be set up.
static float read_channel_voltage(adc_channel_t chan) {
    int raw = 0;
    if (adc_oneshot_read(s_adc_handle, chan, &raw) != ESP_OK) {
        return -1.0f;
    }

    if (s_cali_available) {
        int mv = 0;
        if (adc_cali_raw_to_voltage(s_cali_handle, raw, &mv) == ESP_OK) {
            return mv / 1000.0f;
        }
    }

    // Fallback: 12-bit ADC (0-4095), ~3.9V full-scale at ADC_ATTEN_DB_12.
    return (raw * 3.9f) / 4095.0f;
}

// Linear map 'voltage' between [v0%, v100%] volts -> [0, 100] percent,
// clamped. Used for both the accurate DIRECT-mode battery curve and the
// coarse RAIL_SAG-mode proxy (with different min/max meanings - see
// config.h / power_source.h).
static uint8_t voltage_to_percentage(float voltage, float v0_percent, float v100_percent) {
    if (voltage <= v0_percent) return 0;
    if (voltage >= v100_percent) return 100;
    float pct = (voltage - v0_percent) / (v100_percent - v0_percent) * 100.0f;
    return (uint8_t)(pct + 0.5f);
}

static void update_battery_reading(void) {
#if ACTIVE_BATTERY_SENSE_MODE == BATTERY_SENSE_MODE_DIRECT
    // Accurate: dedicated wire tapped straight off the battery.
    float adc_voltage = read_channel_voltage(BATTERY_ADC_CHAN);
    if (adc_voltage < 0.0f) {
        return; // transient ADC read failure - keep last known reading
    }
    s_battery_voltage = adc_voltage * BATTERY_VOLTAGE_DIVIDER_RATIO;
    s_battery_percentage = voltage_to_percentage(s_battery_voltage, BATTERY_VOLTAGE_MIN, BATTERY_VOLTAGE_MAX);

#elif ACTIVE_BATTERY_SENSE_MODE == BATTERY_SENSE_MODE_RAIL_SAG
    // Coarse: no dedicated wire exists, so we watch the SHARED regulated
    // rail (fed by either mains or the battery's boost converter) for the
    // undervoltage sag that happens right before it loses regulation.
    // Only meaningful near 0% - see power_source.h for caveats.
    float adc_voltage = read_channel_voltage(BATTERY_ADC_CHAN);
    if (adc_voltage < 0.0f) {
        return;
    }
    s_battery_voltage = adc_voltage * RAIL_SAG_DIVIDER_RATIO;
    s_battery_percentage = voltage_to_percentage(s_battery_voltage, RAIL_SAG_CRITICAL_VOLTAGE, RAIL_NOMINAL_VOLTAGE);

#elif ACTIVE_BATTERY_SENSE_MODE == BATTERY_SENSE_MODE_DIGITAL_STATUS
    // No analog reading at all - just a binary low-battery status pin.
    // Map it onto the same 0-100 alarm-threshold scale that alarm_manager
    // already understands, without pretending to know the real percentage:
    //   status OK   -> 100% (comfortably above BATTERY_LOW_PERCENT)
    //   status LOW  -> BATTERY_LOW_PERCENT (exactly triggers the low-battery
    //                  alarm; this mode cannot distinguish low from critical)
    int level = gpio_get_level(BATTERY_STATUS_GPIO);
    bool is_low = BATTERY_STATUS_ACTIVE_LOW ? (level == 0) : (level != 0);
    s_battery_voltage = 0.0f; // no analog reading available in this mode
    s_battery_percentage = is_low ? BATTERY_LOW_PERCENT : 100;
#endif
}

bool power_source_battery_reading_is_accurate(void) {
    return ACTIVE_BATTERY_SENSE_MODE == BATTERY_SENSE_MODE_DIRECT;
}

bool power_source_update(void) {
    update_battery_reading(); // SRS3_004

    float voltage = read_channel_voltage(POWER_SOURCE_ADC_CHAN);
    if (voltage < 0.0f) {
        return false; // transient ADC read failure - keep previous confirmed state, try again next cycle
    }
    s_last_voltage = voltage;

    power_source_state_t instantaneous =
        (voltage >= POWER_SOURCE_MAIN_VOLTAGE_THRESHOLD) ? POWER_SOURCE_MAIN : POWER_SOURCE_BATTERY;

    int64_t now = millis64();

    if (s_first_sample) {
        // Seed the filter with the real, current reading on boot so we
        // report the correct initial power source immediately instead of
        // defaulting to "battery" for a whole debounce window.
        s_candidate_state    = instantaneous;
        s_candidate_since_ms = now;
        s_confirmed_state    = instantaneous;
        s_first_sample       = false;
        return false;
    }

    if (instantaneous != s_candidate_state) {
        // The reading changed -> restart the debounce timer for the new candidate.
        s_candidate_state    = instantaneous;
        s_candidate_since_ms = now;
        return false;
    }

    // The candidate reading has been stable; accept it as the confirmed
    // state only once it has held for the full debounce window.
    if (s_candidate_state != s_confirmed_state &&
        (now - s_candidate_since_ms) >= POWER_SOURCE_DEBOUNCE_WINDOW_MS) {
        s_confirmed_state = s_candidate_state;
        return true; // confirmed power source changed
    }

    return false;
}

power_source_state_t power_source_get_state(void) {
    return s_confirmed_state;
}

bool power_source_is_main(void) {
    return s_confirmed_state == POWER_SOURCE_MAIN;
}

bool power_source_is_battery(void) {
    return s_confirmed_state == POWER_SOURCE_BATTERY;
}

float power_source_get_last_voltage(void) {
    return s_last_voltage;
}

const char *power_source_get_state_name(void) {
    return (s_confirmed_state == POWER_SOURCE_MAIN) ? "MAIN" : "BATTERY";
}

float power_source_get_battery_voltage(void) {
    return s_battery_voltage;
}

uint8_t power_source_get_battery_percentage(void) {
    return s_battery_percentage;
}





