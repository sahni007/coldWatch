idf_component_register(
  SRCS "touch_input.c"
  INCLUDE_DIRS "include"
  REQUIRES common driver esp_driver_spi esp_driver_gpio esp_timer freertos log
)



