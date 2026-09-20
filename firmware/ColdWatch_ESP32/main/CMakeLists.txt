idf_component_register(
  SRCS "main.c"
  INCLUDE_DIRS "."
  REQUIRES
    common
    nvs_storage
    temperature_sensor
    humidity_sensor
    alarm_manager
    power_source
    buzzer
    lcd_i2c
    sms_module
    touch_input
    driver
    esp_timer
    log
    freertos
)


