#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "mqtt_client.h"
#include "device_mqtt.h"

static const char *TAG = "MQTT_Client";
static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool is_mqtt_connected = false;
static uint32_t connection_time = 0;
static char device_mac_str[18] = {0};  // Store MAC address as string

// Helper function to get MAC-based topic
static void get_device_topic(char *topic_buf, size_t buf_size, const char *subtopic)
{
    snprintf(topic_buf, buf_size, "%s/%s/%s", MQTT_TOPIC_PREFIX, device_mac_str, subtopic);
}

// MQTT event handler
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT Connected to broker");
            is_mqtt_connected = true;
            connection_time = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;

            // Publish initial device information
            mqtt_publish_device_info();
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT Disconnected from broker");
            is_mqtt_connected = false;
            break;

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT Message published, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT Error");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(TAG, "Last error code reported from esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
                ESP_LOGE(TAG, "Last tls stack error number: 0x%x", event->error_handle->esp_tls_stack_err);
                ESP_LOGE(TAG, "Last captured errno : %d (%s)",  event->error_handle->esp_transport_sock_errno,
                         strerror(event->error_handle->esp_transport_sock_errno));
            }
            break;

        default:
            ESP_LOGI(TAG, "MQTT Event id: %d", event->event_id);
            break;
    }
}

// Initialize MQTT client
esp_err_t mqtt_client_init(void)
{
    // Get device MAC address and store it
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    snprintf(device_mac_str, sizeof(device_mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    ESP_LOGI(TAG, "Device MAC: %s", device_mac_str);

    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    ESP_LOGI(TAG, "MQTT client initialized");
    return ESP_OK;
}

// Start MQTT client
esp_err_t mqtt_client_start(void)
{
    if (mqtt_client == NULL) {
        ESP_LOGE(TAG, "MQTT client not initialized");
        return ESP_FAIL;
    }

    esp_err_t err = esp_mqtt_client_start(mqtt_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client");
        return err;
    }

    ESP_LOGI(TAG, "MQTT client started");
    return ESP_OK;
}

// Stop MQTT client
esp_err_t mqtt_client_stop(void)
{
    if (mqtt_client == NULL) {
        return ESP_OK;
    }

    esp_err_t err = esp_mqtt_client_stop(mqtt_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop MQTT client");
        return err;
    }

    is_mqtt_connected = false;
    ESP_LOGI(TAG, "MQTT client stopped");
    return ESP_OK;
}

// Check if MQTT is connected
bool mqtt_is_connected(void)
{
    return is_mqtt_connected;
}

// Publish device MAC address
esp_err_t mqtt_publish_mac_address(void)
{
    if (!is_mqtt_connected) {
        ESP_LOGW(TAG, "MQTT not connected, cannot publish MAC");
        return ESP_FAIL;
    }

    char topic[64];
    get_device_topic(topic, sizeof(topic), "mac");

    int msg_id = esp_mqtt_client_publish(mqtt_client, topic, device_mac_str, 0, 1, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to publish MAC address");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Published MAC address to %s: %s", topic, device_mac_str);
    return ESP_OK;
}

// Publish device IP address
esp_err_t mqtt_publish_ip_address(void)
{
    if (!is_mqtt_connected) {
        ESP_LOGW(TAG, "MQTT not connected, cannot publish IP");
        return ESP_FAIL;
    }

    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");

    if (netif == NULL) {
        ESP_LOGW(TAG, "WiFi STA interface not available");
        return ESP_FAIL;
    }

    esp_netif_get_ip_info(netif, &ip_info);

    char ip_str[16];
    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));

    char topic[64];
    get_device_topic(topic, sizeof(topic), "ip");

    int msg_id = esp_mqtt_client_publish(mqtt_client, topic, ip_str, 0, 1, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to publish IP address");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Published IP address to %s: %s", topic, ip_str);
    return ESP_OK;
}

// Publish device uptime
esp_err_t mqtt_publish_uptime(void)
{
    if (!is_mqtt_connected) {
        ESP_LOGW(TAG, "MQTT not connected, cannot publish uptime");
        return ESP_FAIL;
    }

    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;
    uint32_t uptime = current_time - connection_time;

    char uptime_str[32];
    uint32_t hours = uptime / 3600;
    uint32_t minutes = (uptime % 3600) / 60;
    uint32_t seconds = uptime % 60;

    snprintf(uptime_str, sizeof(uptime_str), "%02lu:%02lu:%02lu", hours, minutes, seconds);

    char topic[64];
    get_device_topic(topic, sizeof(topic), "uptime");

    int msg_id = esp_mqtt_client_publish(mqtt_client, topic, uptime_str, 0, 1, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to publish uptime");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Published uptime to %s: %s", topic, uptime_str);
    return ESP_OK;
}

// Publish all device information
esp_err_t mqtt_publish_device_info(void)
{
    if (!is_mqtt_connected) {
        ESP_LOGW(TAG, "MQTT not connected, cannot publish device info");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Publishing device information...");

    // Publish MAC address
    mqtt_publish_mac_address();
    vTaskDelay(pdMS_TO_TICKS(100));

    // Publish IP address
    mqtt_publish_ip_address();
    vTaskDelay(pdMS_TO_TICKS(100));

    // Publish uptime
    mqtt_publish_uptime();

    // Create combined JSON payload
    esp_netif_ip_info_t ip_info = {0};
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        esp_netif_get_ip_info(netif, &ip_info);
    }

    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;
    uint32_t uptime = current_time - connection_time;

    char json_payload[256];
    snprintf(json_payload, sizeof(json_payload),
             "{\"mac\":\"%s\","
             "\"ip\":\"" IPSTR "\","
             "\"uptime\":%lu,"
             "\"online\":true}",
             device_mac_str,
             IP2STR(&ip_info.ip),
             uptime);

    char topic[64];
    get_device_topic(topic, sizeof(topic), "info");

    int msg_id = esp_mqtt_client_publish(mqtt_client, topic, json_payload, 0, 1, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to publish device info JSON");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Published device info to %s: %s", topic, json_payload);
    return ESP_OK;
}
