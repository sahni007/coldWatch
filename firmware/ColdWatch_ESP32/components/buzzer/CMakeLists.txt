idf_component_register(
  SRCS "buzzer.c"
  INCLUDE_DIRS "include"
  REQUIRES common esp_driver_ledc esp_timer
)

