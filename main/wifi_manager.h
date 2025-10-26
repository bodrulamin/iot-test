#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"

#define WIFI_SSID_MAX_LEN 32
#define WIFI_PASS_MAX_LEN 64
#define AP_SSID "ESP32-Setup"
#define AP_PASSWORD "12345678"
#define MAX_RETRY 5

typedef struct {
    char ssid[WIFI_SSID_MAX_LEN];
    char password[WIFI_PASS_MAX_LEN];
} wifi_credentials_t;

esp_err_t wifi_manager_init(void);
bool wifi_manager_check_credentials(void);
esp_err_t wifi_manager_save_credentials(const char *ssid, const char *password);
esp_err_t wifi_manager_connect_sta(void);
esp_err_t wifi_manager_start_ap(void);
void wifi_manager_start_webserver(void);
bool wifi_manager_is_in_ap_fallback(void);
esp_err_t wifi_manager_clear_credentials(void);

#endif // WIFI_MANAGER_H
