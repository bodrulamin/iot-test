#include "dns_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "esp_log.h"
#include <string.h>

#define DNS_PORT 53
#define DNS_MAX_LEN 256

static const char *TAG = "DNS_Server";
static int dns_socket = -1;
static TaskHandle_t dns_task_handle = NULL;

// DNS header structure
typedef struct {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} dns_header_t;

static void dns_server_task(void *pvParameters)
{
    dns_server_config_t *config = (dns_server_config_t *)pvParameters;
    char rx_buffer[DNS_MAX_LEN];
    char addr_str[128];
    int addr_family;
    int ip_protocol;

    // Small delay to allow network stack to initialize (500ms)
    vTaskDelay(50);

    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(DNS_PORT);
    addr_family = AF_INET;
    ip_protocol = IPPROTO_IP;

    dns_socket = socket(addr_family, SOCK_DGRAM, ip_protocol);
    if (dns_socket < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        dns_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    // Set socket receive timeout
    struct timeval timeout;
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;
    setsockopt(dns_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    int err = bind(dns_socket, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err < 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        close(dns_socket);
        dns_socket = -1;
        dns_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "DNS Server started on port %d", DNS_PORT);

    while (1) {
        struct sockaddr_in source_addr;
        socklen_t socklen = sizeof(source_addr);
        int len = recvfrom(dns_socket, rx_buffer, sizeof(rx_buffer) - 1, 0,
                          (struct sockaddr *)&source_addr, &socklen);

        if (len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Timeout, continue
                continue;
            }
            ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
            break;
        } else {
            inet_ntoa_r(source_addr.sin_addr, addr_str, sizeof(addr_str) - 1);

            if (len >= sizeof(dns_header_t)) {
                dns_header_t *header = (dns_header_t *)rx_buffer;

                // Build DNS response
                uint8_t response[DNS_MAX_LEN];
                memcpy(response, rx_buffer, len);

                dns_header_t *resp_header = (dns_header_t *)response;
                resp_header->flags = htons(0x8180); // Standard query response, no error
                resp_header->ancount = htons(1);    // One answer

                int response_len = len;

                // Add answer section
                // Name (pointer to question)
                response[response_len++] = 0xC0;
                response[response_len++] = 0x0C;

                // Type A
                response[response_len++] = 0x00;
                response[response_len++] = 0x01;

                // Class IN
                response[response_len++] = 0x00;
                response[response_len++] = 0x01;

                // TTL
                response[response_len++] = 0x00;
                response[response_len++] = 0x00;
                response[response_len++] = 0x00;
                response[response_len++] = 0x3C; // 60 seconds

                // Data length
                response[response_len++] = 0x00;
                response[response_len++] = 0x04;

                // IP address (192.168.4.1)
                response[response_len++] = 192;
                response[response_len++] = 168;
                response[response_len++] = 4;
                response[response_len++] = 1;

                // Send response
                int err = sendto(dns_socket, response, response_len, 0,
                               (struct sockaddr *)&source_addr, sizeof(source_addr));
                if (err < 0) {
                    ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                }
            }
        }
    }

    if (dns_socket != -1) {
        ESP_LOGE(TAG, "Shutting down socket");
        shutdown(dns_socket, 0);
        close(dns_socket);
    }
    vTaskDelete(NULL);
}

esp_err_t start_dns_server(dns_server_config_t *config)
{
    if (dns_task_handle != NULL) {
        ESP_LOGW(TAG, "DNS server already running");
        return ESP_OK;
    }

    BaseType_t result = xTaskCreate(dns_server_task, "dns_server", 8192, config, 4, &dns_task_handle);
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create DNS server task");
        return ESP_FAIL;
    }
    return ESP_OK;
}

void stop_dns_server(void)
{
    if (dns_task_handle != NULL) {
        if (dns_socket != -1) {
            shutdown(dns_socket, 0);
            close(dns_socket);
            dns_socket = -1;
        }
        vTaskDelete(dns_task_handle);
        dns_task_handle = NULL;
        ESP_LOGI(TAG, "DNS server stopped");
    }
}
