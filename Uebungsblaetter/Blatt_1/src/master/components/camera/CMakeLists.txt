idf_component_register(SRCS "camera.c"
                       INCLUDE_DIRS "include"
                       REQUIRES driver freertos
                       PRIV_REQUIRES esp_driver_i2c esp_driver_gpio log)
