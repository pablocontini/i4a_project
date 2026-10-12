#ifndef ROUTING_TEST_ESP_NETIF_H
#define ROUTING_TEST_ESP_NETIF_H
#include "lwip/netif.h"
typedef struct { struct netif *impl; } esp_netif_t;
void *esp_netif_get_netif_impl(esp_netif_t *handle);
#endif
