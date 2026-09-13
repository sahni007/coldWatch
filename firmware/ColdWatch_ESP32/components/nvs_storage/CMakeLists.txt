idf_component_register(
  SRCS "nvs_storage.c"
  INCLUDE_DIRS "include"
  REQUIRES common nvs_flash esp_timer log
)

