idf_component_register(
  SRCS "sensor_tsys01.c"
  INCLUDE_DIRS "include"
  REQUIRES i2c_bus sensors_common esp_timer
)
