idf_component_register(SRCS "wifi_AP.c"
                       INCLUDE_DIRS "include"
                       REQUIRES camera localization esp_wifi esp_netif esp_event esp_http_server nvs_flash esp_common
                       PRIV_REQUIRES log)
