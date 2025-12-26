idf_component_register(
    SRCS "log_redirect.c"
    INCLUDE_DIRS "include"
    REQUIRES "freertos"
    PRIV_REQUIRES "esp_ringbuf"
)
