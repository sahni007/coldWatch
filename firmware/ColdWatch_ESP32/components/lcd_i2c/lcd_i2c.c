#include "lcd_i2c.h"
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_ili9341.h"

#define TEXT_SCALE 2
#define TEXT_HEIGHT (7 * TEXT_SCALE)
#define TEXT_WIDTH (5 * TEXT_SCALE)

static esp_lcd_panel_handle_t s_panel;

static const uint8_t *font_for_char(char c) {
    static const uint8_t blank[5] = {0, 0, 0, 0, 0};
    static const uint8_t digits[10][5] = {
        {0x3e,0x51,0x49,0x45,0x3e}, {0x00,0x42,0x7f,0x40,0x00},
        {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4b,0x31},
        {0x18,0x14,0x12,0x7f,0x10}, {0x27,0x45,0x45,0x45,0x39},
        {0x3c,0x4a,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
        {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1e}
    };
    static const uint8_t letters[26][5] = {
        {0x7e,0x11,0x11,0x11,0x7e}, {0x7f,0x49,0x49,0x49,0x36},
        {0x3e,0x41,0x41,0x41,0x22}, {0x7f,0x41,0x41,0x22,0x1c},
        {0x7f,0x49,0x49,0x49,0x41}, {0x7f,0x09,0x09,0x09,0x01},
        {0x3e,0x41,0x49,0x49,0x7a}, {0x7f,0x08,0x08,0x08,0x7f},
        {0x00,0x41,0x7f,0x41,0x00}, {0x20,0x40,0x41,0x3f,0x01},
        {0x7f,0x08,0x14,0x22,0x41}, {0x7f,0x40,0x40,0x40,0x40},
        {0x7f,0x02,0x0c,0x02,0x7f}, {0x7f,0x04,0x08,0x10,0x7f},
        {0x3e,0x41,0x41,0x41,0x3e}, {0x7f,0x09,0x09,0x09,0x06},
        {0x3e,0x41,0x51,0x21,0x5e}, {0x7f,0x09,0x19,0x29,0x46},
        {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7f,0x01,0x01},
        {0x3f,0x40,0x40,0x40,0x3f}, {0x1f,0x20,0x40,0x20,0x1f},
        {0x7f,0x20,0x18,0x20,0x7f}, {0x63,0x14,0x08,0x14,0x63},
        {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43}
    };
    static const uint8_t punctuation[][5] = {
        {0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x5f,0x00,0x00},
        {0x00,0x36,0x36,0x00,0x00}, {0x08,0x08,0x08,0x08,0x08},
        {0x00,0x60,0x60,0x00,0x00}, {0x23,0x13,0x08,0x64,0x62},
        {0x00,0x60,0x18,0x06,0x01}, {0x06,0x09,0x09,0x06,0x00}
    };
    if (c >= '0' && c <= '9') return digits[c - '0'];
    if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
    if (c == ' ') return punctuation[0];
    if (c == ':') return punctuation[2];
    if (c == '!') return punctuation[1];
    if (c == '-') return punctuation[3];
    if (c == '.') return punctuation[4];
    if (c == '%') return punctuation[5];
    if (c == '/') return punctuation[6];
    if ((unsigned char)c == 0xb0) return punctuation[7];
    return blank;
}
static inline uint16_t swap16(uint16_t v) {
    return (v << 8) | (v >> 8);
}
#if 0
static void draw_text(uint16_t x, uint16_t y, const char *text, uint16_t color) {
    uint16_t glyph[TEXT_WIDTH * TEXT_HEIGHT];
    while (*text && x + TEXT_WIDTH <= LCD_WIDTH) {
        const uint8_t *bitmap = font_for_char(*text++);
        for (uint16_t row = 0; row < TEXT_HEIGHT; row++) {
            for (uint16_t col = 0; col < TEXT_WIDTH; col++) {
                uint16_t source_col = col / TEXT_SCALE;
                uint16_t source_row = row / TEXT_SCALE;
                glyph[row * TEXT_WIDTH + col] =
                    (bitmap[source_col] & (1 << source_row)) ? color : 0x0000;
            }
        }
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(
            s_panel, x, y, x + TEXT_WIDTH, y + TEXT_HEIGHT, glyph));
        x += TEXT_WIDTH + TEXT_SCALE;
    }
}
    #else
    static void draw_text(uint16_t x, uint16_t y, const char *text, uint16_t color) {
    uint16_t len = strlen(text);
    uint16_t line_width = len * (TEXT_WIDTH + TEXT_SCALE);
    if (x + line_width > LCD_WIDTH) {
        line_width = LCD_WIDTH - x;
        len = line_width / (TEXT_WIDTH + TEXT_SCALE);
    }
    if (len == 0) return;

    static uint16_t glyph[LCD_WIDTH * TEXT_HEIGHT];
    memset(glyph, 0, line_width * TEXT_HEIGHT * sizeof(uint16_t));

    for (uint16_t i = 0; i < len; i++) {
        const uint8_t *bitmap = font_for_char(text[i]);
        uint16_t x_off = i * (TEXT_WIDTH + TEXT_SCALE);
        for (uint16_t row = 0; row < TEXT_HEIGHT; row++) {
            for (uint16_t col = 0; col < TEXT_WIDTH; col++) {
                uint16_t source_col = col / TEXT_SCALE;
                uint16_t source_row = row / TEXT_SCALE;
                glyph[row * line_width + x_off + col] =
                    (bitmap[source_col] & (1 << source_row)) ? color : 0x0000;
            }
        }
    }

    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(
        s_panel, x, y, x + line_width, y + TEXT_HEIGHT, glyph));
}
#endif

static void clear_screen(void) {
    static uint16_t row[LCD_WIDTH * 20];
    memset(row, 0, sizeof(row));
    for (uint16_t y = 0; y < LCD_HEIGHT; y += 20) {
        uint16_t height = (LCD_HEIGHT - y < 20) ? LCD_HEIGHT - y : 20;
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(s_panel, 0, y, LCD_WIDTH, y + height, row));
    }
}

void lcd_init(void) {
    spi_bus_config_t bus_config = {
        .sclk_io_num = LCD_SPI_SCLK_GPIO,
        .mosi_io_num = LCD_SPI_MOSI_GPIO,
        .miso_io_num = LCD_SPI_MISO_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_WIDTH * 20 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_DISABLED));

    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = LCD_SPI_CS_GPIO,
        .dc_gpio_num = LCD_SPI_DC_GPIO,
        .pclk_hz = 20 * 1000 * 1000,
        .spi_mode = 0,
        .trans_queue_depth = 1,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &io));

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_SPI_RESET_GPIO,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io, &panel_config, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
   ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(s_panel, true));
   // ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel, false, false));   

    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    gpio_set_direction(LCD_SPI_BACKLIGHT_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LCD_SPI_BACKLIGHT_GPIO, 1);
    clear_screen();
    draw_text(12, 20, "COLDWATCH", 0xffff);
}

void lcd_show_normal(const char *sensor_type_name, float temperature, bool temp_valid,
                     const char *humidity_sensor_type_name, float humidity, bool humidity_valid) {
    char date_line[32];
    char time_line[32];
    char temperature_line[32];
    char humidity_line[32];
    time_t current_time = time(NULL);
    struct tm current_tm;

    localtime_r(&current_time, &current_tm);
    clear_screen();
    snprintf(date_line, sizeof(date_line), "DATE : %02d/%02d/%02d",
             current_tm.tm_mday, current_tm.tm_mon + 1, (current_tm.tm_year + 1900) % 100);
    snprintf(time_line, sizeof(time_line), "TIME : %02d:%02d:%02d",
             current_tm.tm_hour, current_tm.tm_min, current_tm.tm_sec);
    if (temp_valid) snprintf(temperature_line, sizeof(temperature_line), "TEMP : %04.1f \xb0" "C", temperature);
    else snprintf(temperature_line, sizeof(temperature_line), "TEMP : --.- \xb0" "C");
    if (humidity_valid) snprintf(humidity_line, sizeof(humidity_line), "HUM : %04.1f %%RH", humidity);
    else snprintf(humidity_line, sizeof(humidity_line), "HUM : --.- %%RH");

    draw_text(12, 8, "COLD STORAGE", 0xffff);
    draw_text(12, 38, date_line, 0xffff);
    draw_text(12, 68, time_line, 0xffff);
    draw_text(12, 98, temperature_line, 0x07e0);
    draw_text(12, 128, humidity_line, 0x07ff);
    draw_text(12, 158, "GSM: 4G WIFI: OK", 0xffff);
    draw_text(12, 188, "STATUS: NORMAL", 0x07e0);

    printf("LCD:\nCOLD STORAGE\n%s\n%s\n%s\n%s\nGSM: 4G WIFI: OK\nSTATUS: NORMAL\n",
           date_line, time_line, temperature_line, humidity_line);
    (void)sensor_type_name;
    (void)humidity_sensor_type_name;
}

void lcd_show_alarm(uint16_t alarm_id, const char *text) {
    char line[32];
    clear_screen();
    snprintf(line, sizeof(line), "ALARM %u", alarm_id);
    draw_text(12, 60, line, 0xf800);
    draw_text(12, 115, text, 0xf800);
}
