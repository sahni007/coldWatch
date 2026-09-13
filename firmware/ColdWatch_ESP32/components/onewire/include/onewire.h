/*
 * onewire.h
 * Minimal bit-banged 1-Wire bus master, pure C, for DS18B20 (SRS1_001).
 */
#ifndef COLDWATCH_ONEWIRE_H
#define COLDWATCH_ONEWIRE_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"

void onewire_init(gpio_num_t pin);

// Sends the reset pulse and listens for the device presence pulse.
// Returns true if at least one device responded.
bool onewire_reset(gpio_num_t pin);

void onewire_write_bit(gpio_num_t pin, uint8_t bit);
uint8_t onewire_read_bit(gpio_num_t pin);
void onewire_write_byte(gpio_num_t pin, uint8_t data);
uint8_t onewire_read_byte(gpio_num_t pin);

// Dallas/Maxim CRC-8 (poly 0x8C), used to validate the DS18B20 scratchpad.
uint8_t onewire_crc8(const uint8_t *data, uint8_t len);

#endif // COLDWATCH_ONEWIRE_H

