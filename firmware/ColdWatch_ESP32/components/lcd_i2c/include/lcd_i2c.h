/*
 * lcd_i2c.h
 * SRS1_006/007/010, SRS2_006/007/010: "Display the alarm on the LCD"
 * SRS1_009 / SRS2_009: "display the type of sensor on the LCD (supporting
 * multiple sensor types)"
 * ILI9341 320x240 landscape TFT driver over SPI.
 */
#ifndef COLDWATCH_LCD_I2C_H
#define COLDWATCH_LCD_I2C_H

#include <stdint.h>
#include <stdbool.h>

void lcd_init(void);
void lcd_show_normal(const char *sensor_type_name, float temperature, bool temp_valid,
                      const char *humidity_sensor_type_name, float humidity, bool humidity_valid);
void lcd_show_alarm(uint16_t alarm_id, const char *text);

#endif // COLDWATCH_LCD_I2C_H

