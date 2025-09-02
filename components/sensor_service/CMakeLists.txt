idf_component_register(
  SRCS "sensor_service.c"
  INCLUDE_DIRS "include"
  REQUIRES sensors_common i2c_bus esp_timer
)
