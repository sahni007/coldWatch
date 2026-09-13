/*
 * onewire.c
 * Bit-banged 1-Wire master. Timings per the Maxim DS18B20 datasheet.
 * Uses esp_rom_delay_us() for microsecond busy-wait delays (safe to call
 * with interrupts enabled; short enough to not starve the scheduler).
 */
#include "onewire.h"
#include "esp_rom_sys.h" // esp_rom_delay_us

static inline void pin_low_output(gpio_num_t pin) {
    gpio_set_direction(pin, GPIO_MODE_OUTPUT_OD);
    gpio_set_level(pin, 0);
}

static inline void pin_release(gpio_num_t pin) {
    // Open-drain output released high (external 4.7k pull-up brings it high)
    gpio_set_direction(pin, GPIO_MODE_OUTPUT_OD);
    gpio_set_level(pin, 1);
}

void onewire_init(gpio_num_t pin) {
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT_OD);
    gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
    gpio_set_level(pin, 1);
}

bool onewire_reset(gpio_num_t pin) {
    pin_low_output(pin);
    esp_rom_delay_us(480);        // reset pulse, low >= 480us
    pin_release(pin);
    esp_rom_delay_us(70);         // wait for device presence pulse
    int presence = gpio_get_level(pin); // 0 = device present
    esp_rom_delay_us(410);        // finish the 480us+ reset slot
    return (presence == 0);
}

void onewire_write_bit(gpio_num_t pin, uint8_t bit) {
    pin_low_output(pin);
    if (bit) {
        esp_rom_delay_us(6);      // short low pulse for a '1'
        pin_release(pin);
        esp_rom_delay_us(64);
    } else {
        esp_rom_delay_us(60);     // long low pulse for a '0'
        pin_release(pin);
        esp_rom_delay_us(10);
    }
}

uint8_t onewire_read_bit(gpio_num_t pin) {
    uint8_t bit;
    pin_low_output(pin);
    esp_rom_delay_us(2);
    pin_release(pin);
    esp_rom_delay_us(10);
    bit = gpio_get_level(pin);
    esp_rom_delay_us(50);
    return bit;
}

void onewire_write_byte(gpio_num_t pin, uint8_t data) {
    for (uint8_t i = 0; i < 8; i++) {
        onewire_write_bit(pin, data & 0x01);
        data >>= 1;
    }
}

uint8_t onewire_read_byte(gpio_num_t pin) {
    uint8_t data = 0;
    for (uint8_t i = 0; i < 8; i++) {
        data |= (onewire_read_bit(pin) << i);
    }
    return data;
}

uint8_t onewire_crc8(const uint8_t *data, uint8_t len) {
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; i++) {
        uint8_t inbyte = data[i];
        for (uint8_t b = 0; b < 8; b++) {
            uint8_t mix = (crc ^ inbyte) & 0x01;
            crc >>= 1;
            if (mix) crc ^= 0x8C;
            inbyte >>= 1;
        }
    }
    return crc;
}

