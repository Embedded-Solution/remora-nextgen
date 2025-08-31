idf_component_register(
    SRCS "app_upload.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_client wifi_net
)
