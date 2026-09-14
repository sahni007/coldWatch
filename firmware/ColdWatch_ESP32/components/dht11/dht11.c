/*
 * dht11.c
 * Bit-banged DHT11 driver. Timings per the DHT11 datasheet:
 *
 *   1. Host pulls the line low for >= 18ms (start signal), then releases it.
 *   2. Sensor responds: pulls low ~80us, then high ~80us.
 *   3. Sensor then clocks out 40 bits (5 bytes: humidity int, humidity dec,
 *      temp int, temp dec, checksum). Each bit is a ~50us low pulse
 *      followed by a high pulse whose length encodes the bit:
 *        ~26-28us high -> '0'
 *        ~70us    high -> '1'
 *   4. checksum == sum of the first 4 bytes (truncated to 8 bits).
 *
 * Uses esp_rom_delay_us() for short busy-wait delays, same approach as
 * onewire.c.
 */
#include "dht11.h"
#include "esp_rom_sys.h" // esp_rom_delay_us

// Generous timeout for any single wait-for-edge step; if the line never
// transitions within this window we treat it as "sensor not responding".
#define DHT11_WAIT_TIMEOUT_US   200

// Threshold to distinguish a '0' bit (~26-28us high) from a '1' bit
// (~70us high). Halfway between the two is a safe cutoff.
#define DHT11_BIT_THRESHOLD_US   40

static gpio_num_t s_pin;

static inline void pin_low_output(gpio_num_t pin) {
    gpio_set_direction(pin, GPIO_MODE_OUTPUT_OD);
    gpio_set_level(pin, 0);
}

static inline void pin_release(gpio_num_t pin) {
    // Open-drain, released high via the external pull-up.
    gpio_set_direction(pin, GPIO_MODE_OUTPUT_OD);
    gpio_set_level(pin, 1);
}

// Busy-waits while the pin stays at 'level'. Returns the elapsed time in
// microseconds once the pin changes, or -1 if 'timeout_us' is exceeded.
static int wait_while_level(gpio_num_t pin, int level, int timeout_us) {
    int elapsed = 0;
    while (gpio_get_level(pin) == level) {
        if (elapsed >= timeout_us) {
            return -1;
        }
        esp_rom_delay_us(1);
        elapsed++;
    }
    return elapsed;
}

void dht11_init(gpio_num_t pin) {
    s_pin = pin;
    gpio_reset_pin(pin);
    gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
    pin_release(pin);
}

// Runs the handshake + 40-bit read into 'data'. Returns false as soon as any
// expected edge/timing doesn't show up (timeout / disconnected sensor).
// Leaves the bus in whatever state it was in when it returned; the caller
// is responsible for releasing the bus afterwards either way.
static bool read_raw_frame(uint8_t data[5]) {
    // ---- 1. Host start signal: pull low for 20ms (>= 18ms required) ----
    pin_low_output(s_pin);
    esp_rom_delay_us(20000);

    // ---- 2. Release the bus and wait for the sensor's response ----
    pin_release(s_pin);
    esp_rom_delay_us(30);
    gpio_set_direction(s_pin, GPIO_MODE_INPUT);

    if (wait_while_level(s_pin, 1, DHT11_WAIT_TIMEOUT_US) < 0) {
        return false; // bus never went high after release
    }
    if (wait_while_level(s_pin, 0, DHT11_WAIT_TIMEOUT_US) < 0) {
        return false; // missing the ~80us "present" low pulse
    }
    if (wait_while_level(s_pin, 1, DHT11_WAIT_TIMEOUT_US) < 0) {
        return false; // missing the ~80us "present" high pulse
    }

    // ---- 3. Read 40 data bits ----
    for (int i = 0; i < 40; i++) {
        if (wait_while_level(s_pin, 0, DHT11_WAIT_TIMEOUT_US) < 0) {
            return false; // missing the ~50us low lead-in for this bit
        }
        int high_us = wait_while_level(s_pin, 1, DHT11_WAIT_TIMEOUT_US);
        if (high_us < 0) {
            return false;
        }
        data[i / 8] <<= 1;
        if (high_us > DHT11_BIT_THRESHOLD_US) {
            data[i / 8] |= 1;
        }
    }
    return true;
}

bool dht11_read(float *out_temperature_c, float *out_humidity_pct) {
    uint8_t data[5] = {0};

    bool got_frame = read_raw_frame(data);
    pin_release(s_pin); // idle the bus high again for the next read, either way

    if (!got_frame) {
        return false;
    }

    // ---- 4. Validate checksum ----
    uint8_t checksum = (uint8_t)(data[0] + data[1] + data[2] + data[3]);
    if (checksum != data[4]) {
        return false; // corrupted frame
    }

    *out_humidity_pct  = (float)data[0] + (float)data[1] / 10.0f;
    *out_temperature_c = (float)data[2] + (float)data[3] / 10.0f;
    return true;
}



