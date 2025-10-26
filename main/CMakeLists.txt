idf_component_register(SRCS "device_mqtt.c" "main.c"
                            "wifi_manager.c"
                            "dns_server.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_wifi esp_netif esp_http_server nvs_flash lwip mqtt)
