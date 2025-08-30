idf_component_register(
    SRCS "dive_storage.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES json spiffs vfs
)