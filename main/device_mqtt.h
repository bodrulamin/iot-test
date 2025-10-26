#ifndef DEVICE_MQTT_H
#define DEVICE_MQTT_H

#include "esp_err.h"
#include <stdbool.h>

// MQTT broker configuration
#define MQTT_BROKER_URI "mqtt://itbir.com"
#define MQTT_BROKER_PORT 1883

// MQTT topic prefix - topics will be: devices/MAC_ADDRESS/info, devices/MAC_ADDRESS/ip, etc.
#define MQTT_TOPIC_PREFIX "devices"

// Initialize MQTT client
esp_err_t mqtt_client_init(void);

// Start MQTT client (connects to broker)
esp_err_t mqtt_client_start(void);

// Stop MQTT client
esp_err_t mqtt_client_stop(void);

// Publish device information
esp_err_t mqtt_publish_device_info(void);

// Publish device MAC address
esp_err_t mqtt_publish_mac_address(void);

// Publish device IP address
esp_err_t mqtt_publish_ip_address(void);

// Publish device uptime
esp_err_t mqtt_publish_uptime(void);

// Check if MQTT is connected
bool mqtt_is_connected(void);

#endif // DEVICE_MQTT_H
