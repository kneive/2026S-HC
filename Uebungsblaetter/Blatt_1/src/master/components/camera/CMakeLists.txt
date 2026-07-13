idf_component_register(SRCS "camera.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp32-camera esp_common
                       PRIV_REQUIRES esp_driver_gpio esp_driver_i2c freertos log)
