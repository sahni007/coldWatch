/*
 * dht11.h
 * Minimal bit-banged single-wire driver for the DHT11 temperature/humidity
 * sensor, pure C (no Arduino "DHT" library). Not to be confused with the
 * Dallas/Maxim OneWire bus used by the DS18B20 — DHT11 uses its own,
 * simpler single-wire protocol with different timings.
 */
#ifndef COLDWATCH_DHT11_H
#define COLDWATCH_DHT11_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"

// Configure the GPIO used for the DHT11 data line. The line needs an
// external ~4.7k-10k pull-up to 3.3V (most DHT11 breakout boards already
// include one).
void dht11_init(gpio_num_t pin);

// Take one reading from the sensor.
// Returns true and fills *out_temperature_c / *out_humidity_pct on success.
// Returns false on timeout / checksum failure (e.g. no response, wiring
// fault, or reading too soon after the previous sample — DHT11 needs
// >= 1s between reads).
bool dht11_read(float *out_temperature_c, float *out_humidity_pct);

#endif // COLDWATCH_DHT11_H

