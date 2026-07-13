idf_component_register(SRCS "receiver_i2c_bus.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_i2c esp_driver_gpio esp_common
                       PRIV_REQUIRES freertos log)
