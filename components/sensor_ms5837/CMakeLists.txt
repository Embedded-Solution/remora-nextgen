idf_component_register(
  SRCS "sensor_ms5837.c"
  INCLUDE_DIRS "include"
  REQUIRES i2c_bus sensors_common esp_timer
)
