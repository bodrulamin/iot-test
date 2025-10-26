#include <string.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_http_server.h"
#include "esp_netif.h"

#include "wifi_manager.h"

static const char *TAG = "WiFi_Manager";
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static httpd_handle_t server = NULL;
static bool fallback_to_ap_mode = false;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define NVS_NAMESPACE "wifi_config"
#define MAX_SCAN_RESULTS 20

// Scan state management
static bool is_scanning = false;
static wifi_ap_record_t cached_scan_results[MAX_SCAN_RESULTS];
static uint16_t cached_scan_count = 0;
static bool has_cached_results = false;

// HTML pages
static const char *html_header =
    "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>body{font-family:Arial;margin:20px;background:#f0f0f0}"
    ".container{max-width:600px;margin:auto;background:white;padding:20px;border-radius:10px;box-shadow:0 2px 5px rgba(0,0,0,0.1)}"
    "h1{color:#333;text-align:center}button,input[type=submit]{background:#4CAF50;color:white;padding:10px 20px;"
    "border:none;border-radius:5px;cursor:pointer;width:100%;margin-top:10px}button:hover{background:#45a049}"
    "input[type=text],input[type=password]{width:100%;padding:10px;margin:8px 0;border:1px solid #ddd;border-radius:4px;box-sizing:border-box}"
    ".menu{background:#333;padding:10px;border-radius:5px;margin-bottom:20px}"
    ".menu a{color:white;text-decoration:none;padding:10px 15px;display:inline-block}"
    ".menu a:hover{background:#555;border-radius:3px}"
    ".wifi-list{list-style:none;padding:0}.wifi-item{background:#f9f9f9;margin:5px 0;padding:10px;border-radius:5px;"
    "cursor:pointer;border:1px solid #ddd}.wifi-item:hover{background:#e9e9e9}"
    ".info-row{padding:8px;border-bottom:1px solid #eee}.label{font-weight:bold;color:#666}</style></head><body>";

static const char *html_footer = "</body></html>";

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry connecting to WiFi... (Attempt %d/%d)", s_retry_num, MAX_RETRY);
        } else {
            ESP_LOGE(TAG, "Failed to connect to WiFi after %d retries", MAX_RETRY);
            if (s_wifi_event_group) {
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            }
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        if (s_wifi_event_group) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
    }
}

// Save WiFi credentials to NVS
esp_err_t wifi_manager_save_credentials(const char *ssid, const char *password)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS");
        return err;
    }

    err = nvs_set_str(nvs_handle, "ssid", ssid);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_set_str(nvs_handle, "password", password);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "WiFi credentials saved");
    return err;
}

// Load WiFi credentials from NVS
static esp_err_t wifi_manager_load_credentials(wifi_credentials_t *creds)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;
    size_t ssid_len = WIFI_SSID_MAX_LEN;
    size_t pass_len = WIFI_PASS_MAX_LEN;

    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_str(nvs_handle, "ssid", creds->ssid, &ssid_len);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_get_str(nvs_handle, "password", creds->password, &pass_len);
    nvs_close(nvs_handle);

    return err;
}

// Check if credentials exist
bool wifi_manager_check_credentials(void)
{
    wifi_credentials_t creds;
    esp_err_t err = wifi_manager_load_credentials(&creds);
    return (err == ESP_OK && strlen(creds.ssid) > 0);
}

// Check if system is in AP fallback mode
bool wifi_manager_is_in_ap_fallback(void)
{
    return fallback_to_ap_mode;
}

// Clear saved WiFi credentials
esp_err_t wifi_manager_clear_credentials(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS");
        return err;
    }

    nvs_erase_key(nvs_handle, "ssid");
    nvs_erase_key(nvs_handle, "password");
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "WiFi credentials cleared");
    return err;
}

// HTTP handler for main page
static esp_err_t root_handler(httpd_req_t *req)
{
    char *response = malloc(2048);
    if (response == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    snprintf(response, 2048,
        "%s<div class='container'>"
        "<div class='menu'><a href='/'>WiFi Setup</a><a href='/info'>Device Info</a></div>"
        "<h1>ESP32 WiFi Setup</h1>"
        "<p style='text-align:center'>Scan and connect to WiFi network</p>"
        "<form action='/scan' method='get'><button type='submit'>Scan WiFi Networks</button></form>"
        "<div id='networks'></div>"
        "</div>%s",
        html_header, html_footer);

    httpd_resp_send(req, response, strlen(response));
    free(response);
    return ESP_OK;
}

// Custom error handler for 404 errors
static esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    // Just send a simple 404 response without crashing
    httpd_resp_set_status(req, "404 Not Found");
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "404 Not Found", -1);
    return ESP_OK;
}

// Captive portal handler - redirect to main page
static esp_err_t captive_handler(httpd_req_t *req)
{
    // Redirect to root page
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// HTTP handler for WiFi scan
static esp_err_t scan_handler(httpd_req_t *req)
{
    // Check query parameter to see if user wants to rescan
    char query[64] = {0};
    bool should_scan = false;

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char param[8];
        if (httpd_query_key_value(query, "rescan", param, sizeof(param)) == ESP_OK) {
            should_scan = true;
        }
    }

    // If no cached results or user requested rescan
    if (!has_cached_results || should_scan) {
        // Check if scan is already in progress
        if (is_scanning) {
            char *response = malloc(1536);
            if (response == NULL) {
                httpd_resp_send_500(req);
                return ESP_FAIL;
            }
            snprintf(response, 1536,
                "%s<div class='container'>"
                "<div class='menu'><a href='/'>WiFi Setup</a><a href='/info'>Device Info</a></div>"
                "<h1>Scanning in Progress...</h1>"
                "<p>Please wait, WiFi scan is already running.</p>"
                "<form action='/scan' method='get'><button>Refresh</button></form>"
                "</div>%s",
                html_header, html_footer);
            httpd_resp_send(req, response, strlen(response));
            free(response);
            return ESP_OK;
        }

        // Start new scan
        is_scanning = true;

        wifi_scan_config_t scan_config = {
            .ssid = NULL,
            .bssid = NULL,
            .channel = 0,
            .show_hidden = true
        };

        ESP_LOGI(TAG, "Starting WiFi scan...");
        esp_err_t err = esp_wifi_scan_start(&scan_config, true);

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "WiFi scan failed: %d", err);
            is_scanning = false;
            char *response = malloc(1536);
            if (response == NULL) {
                httpd_resp_send_500(req);
                return ESP_FAIL;
            }
            snprintf(response, 1536,
                "%s<div class='container'>"
                "<div class='menu'><a href='/'>WiFi Setup</a><a href='/info'>Device Info</a></div>"
                "<h1>Scan Failed</h1>"
                "<p>WiFi scan failed. Please try again.</p>"
                "<form action='/scan' method='get'><button>Retry</button></form>"
                "</div>%s",
                html_header, html_footer);
            httpd_resp_send(req, response, strlen(response));
            free(response);
            return ESP_OK;
        }

        // Get scan results
        uint16_t ap_count = 0;
        esp_wifi_scan_get_ap_num(&ap_count);

        if (ap_count > MAX_SCAN_RESULTS) {
            ap_count = MAX_SCAN_RESULTS;
        }

        // Cache the results
        cached_scan_count = ap_count;
        if (ap_count > 0) {
            esp_wifi_scan_get_ap_records(&cached_scan_count, cached_scan_results);
            has_cached_results = true;
        } else {
            has_cached_results = false;
        }

        is_scanning = false;
        ESP_LOGI(TAG, "WiFi scan completed. Found %d networks", ap_count);
    }

    // Show results from cache
    if (cached_scan_count == 0 || !has_cached_results) {
        char *response = malloc(1536);
        if (response == NULL) {
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        snprintf(response, 1536,
            "%s<div class='container'>"
            "<div class='menu'><a href='/'>WiFi Setup</a><a href='/info'>Device Info</a></div>"
            "<h1>No Networks Found</h1>"
            "<p>No WiFi networks detected. Try scanning again.</p>"
            "<form action='/scan?rescan=1' method='get'><button>Scan Again</button></form>"
            "<form action='/' method='get' style='margin-top:10px'><button>Back</button></form>"
            "</div>%s",
            html_header, html_footer);
        httpd_resp_send(req, response, strlen(response));
        free(response);
        return ESP_OK;
    }

    // Display cached scan results
    char *response = malloc(8192);
    if (response == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    int len = snprintf(response, 8192,
        "%s<div class='container'>"
        "<div class='menu'><a href='/'>WiFi Setup</a><a href='/info'>Device Info</a></div>"
        "<h1>Available Networks</h1>"
        "<p style='text-align:center;color:#666'>%d networks found</p>"
        "<ul class='wifi-list'>",
        html_header, cached_scan_count);

    for (int i = 0; i < cached_scan_count; i++) {
        len += snprintf(response + len, 8192 - len,
            "<li class='wifi-item' onclick=\"document.getElementById('ssid').value='%s'\">"
            "%s (RSSI: %d)</li>",
            (char *)cached_scan_results[i].ssid,
            (char *)cached_scan_results[i].ssid,
            cached_scan_results[i].rssi);
    }

    len += snprintf(response + len, 8192 - len,
        "</ul><h3>Connect to Network</h3>"
        "<form action='/connect' method='get'>"
        "SSID: <input type='text' name='ssid' id='ssid' required><br>"
        "Password: <input type='password' name='password'><br>"
        "<input type='submit' value='Connect'></form>"
        "<form action='/scan?rescan=1' method='get' style='margin-top:10px'>"
        "<button type='submit'>Scan Again</button></form>"
        "</div>%s", html_footer);

    httpd_resp_send(req, response, strlen(response));
    free(response);
    return ESP_OK;
}

// URL decode helper function
static void url_decode(char *dst, const char *src)
{
    char a, b;
    while (*src) {
        if ((*src == '%') &&
            ((a = src[1]) && (b = src[2])) &&
            (isxdigit(a) && isxdigit(b))) {
            if (a >= 'a')
                a -= 'a'-'A';
            if (a >= 'A')
                a -= ('A' - 10);
            else
                a -= '0';
            if (b >= 'a')
                b -= 'a'-'A';
            if (b >= 'A')
                b -= ('A' - 10);
            else
                b -= '0';
            *dst++ = 16*a+b;
            src+=3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst++ = '\0';
}

// HTTP handler for WiFi connect
static esp_err_t connect_handler(httpd_req_t *req)
{
    char buf[256];
    char ssid[WIFI_SSID_MAX_LEN] = {0};
    char password[WIFI_PASS_MAX_LEN] = {0};

    int ret = httpd_req_get_url_query_str(req, buf, sizeof(buf));
    if (ret == ESP_OK) {
        char param[64];
        if (httpd_query_key_value(buf, "ssid", param, sizeof(param)) == ESP_OK) {
            url_decode(ssid, param);
        }
        if (httpd_query_key_value(buf, "password", param, sizeof(param)) == ESP_OK) {
            url_decode(password, param);
        }
    }

    ESP_LOGI(TAG, "Connecting to SSID: %s, Password: %s", ssid, password);

    wifi_manager_save_credentials(ssid, password);

    char *response = malloc(1536);
    if (response == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    snprintf(response, 1536,
        "%s<div class='container'>"
        "<div class='menu'><a href='/'>WiFi Setup</a><a href='/info'>Device Info</a></div>"
        "<h1>Connecting...</h1>"
        "<p>ESP32 is connecting to <b>%s</b></p>"
        "<p>The device will restart in 3 seconds...</p>"
        "<p>If connection is successful, this page will no longer be available.</p>"
        "</div><script>setTimeout(function(){window.location='/info';},5000);</script>%s",
        html_header, ssid, html_footer);

    httpd_resp_send(req, response, strlen(response));
    free(response);

    vTaskDelay(50);  // 500ms delay
    esp_restart();

    return ESP_OK;
}

// HTTP handler for device info page
static esp_err_t info_handler(httpd_req_t *req)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);

    wifi_credentials_t creds = {0};
    wifi_manager_load_credentials(&creds);

    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (netif == NULL) {
        netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    }
    esp_netif_get_ip_info(netif, &ip_info);

    char *response = malloc(2048);
    if (response == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    snprintf(response, 2048,
        "%s<div class='container'>"
        "<div class='menu'><a href='/'>WiFi Setup</a><a href='/info'>Device Info</a></div>"
        "<h1>Device Information</h1>"
        "<div class='info-row'><span class='label'>Chip Model:</span> ESP32</div>"
        "<div class='info-row'><span class='label'>Cores:</span> %d</div>"
        "<div class='info-row'><span class='label'>Revision:</span> %d</div>"
        "<div class='info-row'><span class='label'>Features:</span> %s</div>"
        "<div class='info-row'><span class='label'>MAC Address:</span> %02X:%02X:%02X:%02X:%02X:%02X</div>"
        "<div class='info-row'><span class='label'>IP Address:</span> " IPSTR "</div>"
        "<div class='info-row'><span class='label'>Free Heap:</span> %lu bytes</div>"
        "<div class='info-row'><span class='label'>Saved SSID:</span> %s</div>"
        "<form action='/' method='get' style='margin-top:20px'><button>Back to WiFi Setup</button></form>"
        "</div>%s",
        html_header,
        chip_info.cores,
        chip_info.revision,
        (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi" : "N/A",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
        IP2STR(&ip_info.ip),
        (unsigned long)esp_get_free_heap_size(),
        strlen(creds.ssid) > 0 ? creds.ssid : "None",
        html_footer);

    httpd_resp_send(req, response, strlen(response));
    free(response);
    return ESP_OK;
}

// Start web server
void wifi_manager_start_webserver(void)
{
    if (server != NULL) {
        return;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.stack_size = 8192;  // Increase stack size from default 4096 to prevent overflow
    config.max_uri_handlers = 16;
    config.max_resp_headers = 8;

    ESP_LOGI(TAG, "Starting web server");
    if (httpd_start(&server, &config) == ESP_OK) {
        // Register custom error handler to prevent crashes on unknown URLs
        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler);

        httpd_uri_t root = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = root_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &root);

        httpd_uri_t scan = {
            .uri       = "/scan",
            .method    = HTTP_GET,
            .handler   = scan_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &scan);

        httpd_uri_t connect = {
            .uri       = "/connect",
            .method    = HTTP_GET,
            .handler   = connect_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &connect);

        httpd_uri_t info = {
            .uri       = "/info",
            .method    = HTTP_GET,
            .handler   = info_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &info);

        // Captive portal detection URLs - Android
        httpd_uri_t generate_204 = {
            .uri       = "/generate_204",
            .method    = HTTP_GET,
            .handler   = captive_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &generate_204);

        httpd_uri_t gen_204 = {
            .uri       = "/gen_204",
            .method    = HTTP_GET,
            .handler   = captive_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &gen_204);

        // iOS/macOS
        httpd_uri_t hotspot = {
            .uri       = "/hotspot-detect.html",
            .method    = HTTP_GET,
            .handler   = captive_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &hotspot);

        // Windows
        httpd_uri_t ncsi = {
            .uri       = "/ncsi.txt",
            .method    = HTTP_GET,
            .handler   = captive_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &ncsi);

        httpd_uri_t connecttest = {
            .uri       = "/connecttest.txt",
            .method    = HTTP_GET,
            .handler   = captive_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &connecttest);

        // Additional common URLs
        httpd_uri_t redirect = {
            .uri       = "/redirect",
            .method    = HTTP_GET,
            .handler   = captive_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &redirect);
    }
}

// Start WiFi in AP mode
esp_err_t wifi_manager_start_ap(void)
{
    // Check if AP netif already exists, if not create it
    if (esp_netif_get_handle_from_ifkey("WIFI_AP_DEF") == NULL) {
        esp_netif_create_default_wifi_ap();
    }

    // Check if STA netif already exists, if not create it (for scanning)
    if (esp_netif_get_handle_from_ifkey("WIFI_STA_DEF") == NULL) {
        esp_netif_create_default_wifi_sta();
    }

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .channel = 1,
            .password = AP_PASSWORD,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };

    // Use APSTA mode to enable WiFi scanning while AP is running
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP started. SSID: %s, Password: %s", AP_SSID, AP_PASSWORD);
    ESP_LOGI(TAG, "WiFi mode: APSTA (AP + Station for scanning)");

    wifi_manager_start_webserver();

    return ESP_OK;
}

// Connect to WiFi in STA mode
esp_err_t wifi_manager_connect_sta(void)
{
    wifi_credentials_t creds;
    esp_err_t err = wifi_manager_load_credentials(&creds);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load credentials");
        return err;
    }

    // Reset retry counter and fallback flag
    s_retry_num = 0;
    fallback_to_ap_mode = false;

    s_wifi_event_group = xEventGroupCreate();
    esp_netif_create_default_wifi_sta();

    wifi_config_t wifi_config = {0};
    strcpy((char *)wifi_config.sta.ssid, creds.ssid);
    strcpy((char *)wifi_config.sta.password, creds.password);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to WiFi SSID: %s, Password: %s", creds.ssid, creds.password);

    // Wait up to 20 seconds for connection (MAX_RETRY * ~4 seconds per retry)
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            pdMS_TO_TICKS(20000));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to WiFi successfully");
        s_retry_num = 0;
        return ESP_OK;
    } else {
        // Connection failed - stop WiFi and clean up for AP mode
        if (bits & WIFI_FAIL_BIT) {
            ESP_LOGE(TAG, "Failed to connect to WiFi after %d retries", MAX_RETRY);
        } else {
            ESP_LOGE(TAG, "WiFi connection timeout");
        }

        // Stop WiFi and deinit to clean up
        ESP_LOGI(TAG, "Stopping WiFi for cleanup...");
        esp_wifi_stop();
        esp_wifi_deinit();

        // Clean up event group
        if (s_wifi_event_group) {
            vEventGroupDelete(s_wifi_event_group);
            s_wifi_event_group = NULL;
        }

        // Reset retry counter to prevent message loop
        s_retry_num = 0;

        // Re-initialize WiFi for AP mode
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));

        return ESP_FAIL;
    }
}

// Initialize WiFi manager
esp_err_t wifi_manager_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    return ESP_OK;
}
