idf_component_register(
  SRCS "temperature_sensor.c"
  INCLUDE_DIRS "include"
  REQUIRES common onewire driver freertos
)

