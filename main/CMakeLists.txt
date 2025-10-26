idf_component_register(SRCS "main.c"
                            "wifi_manager.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_wifi esp_netif esp_http_server nvs_flash lwip)
