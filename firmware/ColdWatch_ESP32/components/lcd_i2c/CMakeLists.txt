idf_component_register(
  SRCS "lcd_i2c.c"
  INCLUDE_DIRS "include"
  REQUIRES common esp_lcd esp_driver_gpio esp_driver_gpio freertos
)

