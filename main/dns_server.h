#ifndef DNS_SERVER_H
#define DNS_SERVER_H

#include "esp_err.h"

typedef struct {
    char *domain;
    char *ip;
} dns_server_config_t;

#define DNS_SERVER_CONFIG_SINGLE(domain, ip) {.domain = domain, .ip = ip}

esp_err_t start_dns_server(dns_server_config_t *config);
void stop_dns_server(void);

#endif // DNS_SERVER_H
