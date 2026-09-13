idf_component_register(
  SRCS "alarm_manager.c"
  INCLUDE_DIRS "include"
  REQUIRES common nvs_storage buzzer lcd_i2c sms_module
)

