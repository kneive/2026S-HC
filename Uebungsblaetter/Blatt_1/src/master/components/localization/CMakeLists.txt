idf_component_register(SRCS "localization.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES receiver_i2c_bus freertos log)
