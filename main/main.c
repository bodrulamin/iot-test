#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "wifi_manager.h"
#include "device_mqtt.h"

static const char *TAG = "Main";

// Task to periodically publish device information
static void mqtt_publish_task(void *pvParameters)
{
    while (1) {
        // Wait for MQTT connection
        if (mqtt_is_connected()) {
            // Publish device information every 60 seconds
            mqtt_publish_device_info();
        }
        vTaskDelay(pdMS_TO_TICKS(60000)); // 60 seconds
    }
}

void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP32 WiFi Manager Starting...");

    // Initialize WiFi
    ESP_ERROR_CHECK(wifi_manager_init());

    bool is_ap_mode = false;

    // Check if credentials exist
    if (wifi_manager_check_credentials()) {
        ESP_LOGI(TAG, "Found saved WiFi credentials, attempting to connect...");

        // Try to connect to saved WiFi
        if (wifi_manager_connect_sta() == ESP_OK) {
            ESP_LOGI(TAG, "Successfully connected to WiFi!");
            // Start web server for configuration even when connected
            wifi_manager_start_webserver();

            // Initialize and start MQTT client
            ESP_LOGI(TAG, "Initializing MQTT client...");
            if (mqtt_client_init() == ESP_OK) {
                if (mqtt_client_start() == ESP_OK) {
                    ESP_LOGI(TAG, "MQTT client started successfully");
                    // Create task to periodically publish device info
                    xTaskCreate(mqtt_publish_task, "mqtt_publish_task", 4096, NULL, 5, NULL);
                } else {
                    ESP_LOGE(TAG, "Failed to start MQTT client");
                }
            } else {
                ESP_LOGE(TAG, "Failed to initialize MQTT client");
            }
        } else {
            ESP_LOGI(TAG, "Failed to connect, starting AP mode...");
            wifi_manager_start_ap();
            is_ap_mode = true;
        }
    } else {
        ESP_LOGI(TAG, "No saved credentials found, starting AP mode...");
        wifi_manager_start_ap();
        is_ap_mode = true;
    }

    ESP_LOGI(TAG, "System ready!");

    if (is_ap_mode) {
        ESP_LOGI(TAG, "===========================================");
        ESP_LOGI(TAG, "AP Mode - Connect to configure WiFi:");
        ESP_LOGI(TAG, "  WiFi SSID: %s", AP_SSID);
        ESP_LOGI(TAG, "  Password: %s", AP_PASSWORD);
        ESP_LOGI(TAG, "  URL: http://192.168.4.1");
        ESP_LOGI(TAG, "===========================================");
    } else {
        ESP_LOGI(TAG, "Connected to WiFi - Web interface available at your IP address");
    }
}
