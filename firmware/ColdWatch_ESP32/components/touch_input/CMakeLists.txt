idf_component_register(
  SRCS "touch_input.c"
  INCLUDE_DIRS "include"
  REQUIRES common esp_driver_gpio esp_driver_spi esp_timer freertos log
)



