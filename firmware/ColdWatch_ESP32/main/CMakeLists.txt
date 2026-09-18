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
    esp_driver_gpio
    esp_driver_uart
    esp_timer
    log
    freertos
)


