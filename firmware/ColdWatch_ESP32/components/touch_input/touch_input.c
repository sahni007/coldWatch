/*
 * touch_input.c
 * XPT2046 touch controller wrapper (shares the TFT's already-initialized
 * SPI2_HOST bus - see lcd_i2c.c lcd_init(), which MUST run first).
 */
#include "touch_input.h"
#include "config.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_xpt2046.h"
#include "esp_lcd_panel_io.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_err.h"

static const char *TAG = "TOUCH_INPUT";
static esp_lcd_touch_handle_t s_touch = NULL;
static bool    s_was_pressed = false;
static int64_t s_last_tap_ms = 0;

static inline int64_t millis64(void) {
    return esp_timer_get_time() / 1000;
}

void touch_input_init(void) {
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_spi_config_t tp_io_config =
        ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(TOUCH_SPI_CS_GPIO);

    esp_err_t err = esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)SPI2_HOST, &tp_io_config, &tp_io_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Touch panel IO init failed (err=%s) - touch navigation disabled",
                 esp_err_to_name(err));
        return;
    }

    // IMPORTANT: the XPT2046 driver scales raw ADC readings to 0..x_max /
    // 0..y_max BEFORE swap_xy is applied; the esp_lcd_touch core then
    // implements swap_xy by simply EXCHANGING the already-scaled x/y values
    // (see esp_lcd_touch.c). That means when TOUCH_SWAP_XY is enabled, the
    // final reported x actually came from the pre-swap value scaled to
    // y_max, and the final y came from the pre-swap value scaled to x_max.
    // So x_max/y_max must be passed pre-swapped here, otherwise the final
    // y range gets capped at LCD_WIDTH (240) instead of LCD_HEIGHT (320) -
    // which made the nav bar (y >= NAV_BAR_Y_TOP = 296) unreachable and
    // PREV/NEXT taps silently never registered.
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = TOUCH_SWAP_XY ? LCD_HEIGHT : LCD_WIDTH,
        .y_max = TOUCH_SWAP_XY ? LCD_WIDTH  : LCD_HEIGHT,
        .rst_gpio_num = -1,        // XPT2046 has no reset pin
        .int_gpio_num = TOUCH_IRQ_GPIO,
        .flags = {
            .swap_xy  = TOUCH_SWAP_XY,
            .mirror_x = TOUCH_MIRROR_X,
            .mirror_y = TOUCH_MIRROR_Y,
        },
    };

    err = esp_lcd_touch_new_spi_xpt2046(tp_io_handle, &tp_cfg, &s_touch);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "XPT2046 driver init failed (err=%s) - touch navigation disabled",
                 esp_err_to_name(err));
        s_touch = NULL;
        return;
    }

    ESP_LOGI(TAG, "XPT2046 touch controller initialized (CS=GPIO%d, IRQ=GPIO%d)",
             TOUCH_SPI_CS_GPIO, TOUCH_IRQ_GPIO);
}

bool touch_input_get_point(uint16_t *x, uint16_t *y) {
    if (s_touch == NULL) {
        return false; // touch hardware missing/failed to init - never navigate via touch
    }

    esp_err_t rd_err = esp_lcd_touch_read_data(s_touch); // pull the latest sample from the controller
    if (rd_err != ESP_OK) {
        ESP_LOGW(TAG, "esp_lcd_touch_read_data failed: %s (check CS/SCLK/MOSI/MISO wiring "
                       "and that lcd_init() ran first)", esp_err_to_name(rd_err));
    }

    esp_lcd_touch_point_data_t point = {0};
    uint8_t point_count = 0;
    esp_err_t err = esp_lcd_touch_get_data(s_touch, &point, &point_count, 1);
    bool pressed = (err == ESP_OK) && (point_count > 0);

    // Only report the rising edge (press just started), same debounce
    // philosophy as main.c's handle_ack_button(), so a single physical tap
    // doesn't register as dozens of rapid screen changes.
    bool tap_edge = pressed && !s_was_pressed;
    s_was_pressed = pressed;

    if (!tap_edge) {
        return false;
    }

    // DEBUG: log every raw tap (before the debounce window check below) so
    // you can see in the serial monitor exactly what coordinates the panel
    // reports for a given physical tap location. Compare against the
    // expected LCD panel space (0..LCD_WIDTH-1 x, 0..LCD_HEIGHT-1 y) and the
    // nav bar zone (config.h NAV_BAR_Y_TOP/NAV_PREV_X_MAX/NAV_NEXT_X_MIN). If
    // tapping the bottom-left/right of the screen reports coordinates that
    // don't land at high y / low-or-high x as expected, flip
    // TOUCH_SWAP_XY / TOUCH_MIRROR_X / TOUCH_MIRROR_Y in config.h to match.
    ESP_LOGI(TAG, "[TOUCH] raw tap detected: x=%u y=%u (strength=%u)",
             point.x, point.y, point.strength);

    int64_t now = millis64();
    if (now - s_last_tap_ms < TOUCH_DEBOUNCE_MS) {
        ESP_LOGI(TAG, "[TOUCH] tap ignored - inside debounce window (%lldms since last tap, "
                       "TOUCH_DEBOUNCE_MS=%d)", (long long)(now - s_last_tap_ms), TOUCH_DEBOUNCE_MS);
        return false;
    }
    s_last_tap_ms = now;

    // Defensive clamp: the XPT2046 ADC->coordinate scaling can occasionally
    // land 1-2 counts past the nominal max at the extreme edges of the
    // panel; clamp so callers (e.g. main.c's y >= NAV_BAR_Y_TOP check) never
    // see an out-of-range value.
    *x = (point.x >= LCD_WIDTH)  ? (LCD_WIDTH  - 1) : point.x;
    *y = (point.y >= LCD_HEIGHT) ? (LCD_HEIGHT - 1) : point.y;
    return true;
}



