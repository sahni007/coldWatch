/*
 * buzzer.c
 */
#include "buzzer.h"
#include "config.h"
#include "driver/ledc.h"
#include "esp_timer.h"

static bool s_active = false;
static bool s_tone_on = false;
static uint16_t s_freq = 0;
static int64_t s_last_toggle_us = 0;

void buzzer_init(void) {
    ledc_timer_config_t timer_conf = {
        .speed_mode      = BUZZER_LEDC_MODE,
        .timer_num       = BUZZER_LEDC_TIMER,
        .duty_resolution = BUZZER_LEDC_DUTY_RES,
        .freq_hz         = 2000, // placeholder, changed per-alarm in buzzer_start()
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer_conf);

    ledc_channel_config_t channel_conf = {
        .gpio_num   = BUZZER_GPIO,
        .speed_mode = BUZZER_LEDC_MODE,
        .channel    = BUZZER_LEDC_CHANNEL,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = BUZZER_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ledc_channel_config(&channel_conf);
}

static void tone_on(uint16_t freq_hz) {
    ledc_set_freq(BUZZER_LEDC_MODE, BUZZER_LEDC_TIMER, freq_hz);
    ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, BUZZER_LEDC_DUTY_ON);
    ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
}

static void tone_off(void) {
    ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, 0);
    ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
}

void buzzer_start(uint16_t frequency_hz) {
    s_active = true;
    s_freq = frequency_hz;
    s_tone_on = false;
    s_last_toggle_us = 0; // force immediate update() to turn it on
}

void buzzer_stop(void) {
    s_active = false;
    s_tone_on = false;
    tone_off();
}

bool buzzer_is_active(void) {
    return s_active;
}

void buzzer_update(void) {
    if (!s_active) return;

    int64_t now = esp_timer_get_time();
    int64_t interval_us = (s_tone_on ? BUZZER_BEEP_ON_MS : BUZZER_BEEP_OFF_MS) * 1000LL;

    if (now - s_last_toggle_us >= interval_us) {
        s_last_toggle_us = now;
        s_tone_on = !s_tone_on;
        if (s_tone_on) {
            tone_on(s_freq);
        } else {
            tone_off();
        }
    }
}

