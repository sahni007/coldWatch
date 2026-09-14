idf_component_register(
  SRCS "sms_module.c"
  INCLUDE_DIRS "include"
  REQUIRES common esp_driver_uart log freertos
)

