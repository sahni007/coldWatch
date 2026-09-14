/*
 * lcd_i2c.c
 * Standard 4-bit HD44780-over-PCF8574 driver, written directly against the
 * ESP-IDF I2C master driver (no Arduino Wire/LiquidCrystal_I2C dependency).
 */
#include "lcd_i2c.h"
#include "config.h"
#include <string.h>
#include <stdio.h>
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// PCF8574 bit layout used by most common backpacks:
//   P0=RS  P1=RW  P2=EN  P3=Backlight  P4-P7=D4-D7
#define LCD_BIT_RS        0x01
#define LCD_BIT_EN        0x04
#define LCD_BIT_BACKLIGHT 0x08

// HD44780 commands
#define LCD_CMD_CLEAR_DISPLAY   0x01
#define LCD_CMD_RETURN_HOME     0x02
#define LCD_CMD_ENTRY_MODE_SET  0x06
#define LCD_CMD_DISPLAY_ON      0x0C
#define LCD_CMD_FUNCTION_SET_4B 0x28
#define LCD_CMD_SET_DDRAM_ADDR  0x80

static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_lcd_device;

static void i2c_master_init(void) {
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_MASTER_PORT,
        .sda_io_num = I2C_MASTER_SDA_GPIO,
        .scl_io_num = I2C_MASTER_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = 1,
    };
    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = LCD_I2C_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &s_i2c_bus));
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_i2c_bus, &device_config,
                                               &s_lcd_device));
}

static esp_err_t i2c_write_byte_raw(uint8_t data) {
    return i2c_master_transmit(s_lcd_device, &data, 1, 50);
}

static void lcd_pulse_enable(uint8_t data) {
    i2c_write_byte_raw(data | LCD_BIT_EN);
    vTaskDelay(pdMS_TO_TICKS(1));
    i2c_write_byte_raw(data & ~LCD_BIT_EN);
    vTaskDelay(pdMS_TO_TICKS(1));
}

static void lcd_write4bits(uint8_t nibble) {
    uint8_t data = (nibble << 4) | LCD_BIT_BACKLIGHT;
    i2c_write_byte_raw(data);
    lcd_pulse_enable(data);
}

static void lcd_send(uint8_t value, uint8_t mode_rs) {
    uint8_t high_nibble = (value >> 4) & 0x0F;
    uint8_t low_nibble  = value & 0x0F;

    uint8_t data_high = (high_nibble << 4) | LCD_BIT_BACKLIGHT | (mode_rs ? LCD_BIT_RS : 0);
    i2c_write_byte_raw(data_high);
    lcd_pulse_enable(data_high);

    uint8_t data_low = (low_nibble << 4) | LCD_BIT_BACKLIGHT | (mode_rs ? LCD_BIT_RS : 0);
    i2c_write_byte_raw(data_low);
    lcd_pulse_enable(data_low);
}

static void lcd_command(uint8_t cmd) {
    lcd_send(cmd, 0);
}

static void lcd_write_char(char c) {
    lcd_send((uint8_t)c, 1);
}

static void lcd_write_string(const char *s) {
    while (*s) lcd_write_char(*s++);
}

static void lcd_set_cursor(uint8_t col, uint8_t row) {
    static const uint8_t row_offsets[] = {0x00, 0x40};
    if (row >= LCD_ROWS) row = LCD_ROWS - 1;
    lcd_command(LCD_CMD_SET_DDRAM_ADDR | (col + row_offsets[row]));
}

static void lcd_clear(void) {
    lcd_command(LCD_CMD_CLEAR_DISPLAY);
    vTaskDelay(pdMS_TO_TICKS(2));
}

static void lcd_print_padded(const char *s, uint8_t width) {
    uint8_t len = (uint8_t)strlen(s);
    lcd_write_string(s);
    for (uint8_t i = len; i < width; i++) lcd_write_char(' ');
}

void lcd_init(void) {
    i2c_master_init();
    vTaskDelay(pdMS_TO_TICKS(50)); // wait for LCD power-on

    // HD44780 4-bit initialization sequence
    lcd_write4bits(0x03);
    vTaskDelay(pdMS_TO_TICKS(5));
    lcd_write4bits(0x03);
    vTaskDelay(pdMS_TO_TICKS(5));
    lcd_write4bits(0x03);
    vTaskDelay(pdMS_TO_TICKS(1));
    lcd_write4bits(0x02); // switch to 4-bit mode

    lcd_command(LCD_CMD_FUNCTION_SET_4B); // 4-bit, 2 line, 5x8 font
    lcd_command(LCD_CMD_DISPLAY_ON);      // display on, cursor off, blink off
    lcd_clear();
    lcd_command(LCD_CMD_ENTRY_MODE_SET);  // increment cursor, no shift

    lcd_set_cursor(0, 0);
    lcd_write_string("ColdWatch Boot");
    vTaskDelay(pdMS_TO_TICKS(1000));
    lcd_clear();
}

void lcd_show_normal(const char *sensor_type_name, float temperature, bool temp_valid,
                      const char *humidity_sensor_type_name, float humidity, bool humidity_valid) {
    char line0[LCD_COLUMNS + 1];
    char line1[32]; // generous scratch buffer; lcd_print_padded() truncates to LCD_COLUMNS for display

    // SRS1_009 / SRS2_009: show both the temperature and humidity sensor
    // type names (e.g. "DS18B20/DHT11").
    snprintf(line0, sizeof(line0), "%s/%s", sensor_type_name, humidity_sensor_type_name);

    char temp_part[10];
    char hum_part[10];
    if (temp_valid) {
        snprintf(temp_part, sizeof(temp_part), "%.1fC", temperature);
    } else {
        snprintf(temp_part, sizeof(temp_part), "--.-C");
    }
    if (humidity_valid) {
        snprintf(hum_part, sizeof(hum_part), "%.1f%%", humidity);
    } else {
        snprintf(hum_part, sizeof(hum_part), "--.-%%");
    }
    snprintf(line1, sizeof(line1), "T:%s H:%s", temp_part, hum_part);

    lcd_set_cursor(0, 0);
    lcd_print_padded(line0, LCD_COLUMNS);
    lcd_set_cursor(0, 1);
    lcd_print_padded(line1, LCD_COLUMNS);
}

void lcd_show_alarm(uint16_t alarm_id, const char *text) {
    char line0[LCD_COLUMNS + 1];
    snprintf(line0, sizeof(line0), "ALARM %u", alarm_id);

    lcd_set_cursor(0, 0);
    lcd_print_padded(line0, LCD_COLUMNS);
    lcd_set_cursor(0, 1);
    lcd_print_padded(text, LCD_COLUMNS);
}


