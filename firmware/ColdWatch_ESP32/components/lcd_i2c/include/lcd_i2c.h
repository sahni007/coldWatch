/*
 * lcd_i2c.h
 * SRS1_006/007/010: "Display the alarm on the LCD"
 * SRS1_009: "display the type of sensor on the LCD (supporting multiple sensor types)"
 * Driver for a 16x2 HD44780 LCD behind a PCF8574 I2C backpack.
 */
#ifndef COLDWATCH_LCD_I2C_H
#define COLDWATCH_LCD_I2C_H

#include <stdint.h>
#include <stdbool.h>

void lcd_init(void);
void lcd_show_normal(const char *sensor_type_name, float temperature, bool temp_valid);
void lcd_show_alarm(uint16_t alarm_id, const char *text);

#endif // COLDWATCH_LCD_I2C_H

