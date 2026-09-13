/*
 * buzzer.h
 * Non-blocking beep pattern generator using the ESP32 LEDC (PWM) peripheral.
 * SRS1_006 / SRS1_007: "Activate the buzzer at a defined frequency"
 */
#ifndef COLDWATCH_BUZZER_H
#define COLDWATCH_BUZZER_H

#include <stdint.h>
#include <stdbool.h>

void buzzer_init(void);
void buzzer_start(uint16_t frequency_hz); // begin beeping at given frequency
void buzzer_stop(void);                   // silence the buzzer
void buzzer_update(void);                 // call periodically to drive the on/off pattern
bool buzzer_is_active(void);

#endif // COLDWATCH_BUZZER_H

