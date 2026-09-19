/*
 * touch_input.h
 * Thin wrapper around the XPT2046 resistive touch controller (shares the
 * TFT's SPI bus - see config.h TOUCH_SPI_CS_GPIO/TOUCH_IRQ_GPIO), used to
 * navigate the 4-screen LCD UI (HOME / ALARMS / POWER / GSM) without any
 * physical buttons.
 */
#ifndef COLDWATCH_TOUCH_INPUT_H
#define COLDWATCH_TOUCH_INPUT_H

#include <stdint.h>
#include <stdbool.h>

// Must be called once, AFTER lcd_init() (reuses the same already-initialized
// SPI bus - see lcd_i2c.c lcd_init()).
void touch_input_init(void);

// Returns true exactly once per physical tap (rising edge of "pressed",
// internally debounced by TOUCH_DEBOUNCE_MS - config.h), and fills *x/*y
// with the touch point already converted into LCD panel coordinates
// (0..LCD_WIDTH-1, 0..LCD_HEIGHT-1 - the same space used by draw_text()).
// Returns false (and leaves *x/*y untouched) if there is no new tap.
bool touch_input_get_point(uint16_t *x, uint16_t *y);

#endif // COLDWATCH_TOUCH_INPUT_H

