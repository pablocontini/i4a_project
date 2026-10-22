#ifndef NATIVE_PORTAL_ESP_NETIF_H
#define NATIVE_PORTAL_ESP_NETIF_H
#include <stdint.h>
#include "esp_err.h"
typedef struct { uint32_t addr; } esp_ip4_addr_t;
typedef struct {
    esp_ip4_addr_t ip, netmask, gw;
} esp_netif_ip_info_t;
typedef struct { esp_netif_ip_info_t ip; } esp_netif_t;
typedef struct { esp_netif_ip_info_t ip_info; } ip_event_got_ip_t;
esp_netif_t *esp_netif_create_default_wifi_sta(void);
esp_err_t esp_netif_dhcpc_start(esp_netif_t *);
esp_err_t esp_netif_dhcpc_stop(esp_netif_t *);
esp_err_t esp_netif_set_ip_info(esp_netif_t *, const esp_netif_ip_info_t *);
void esp_netif_destroy_default_wifi(esp_netif_t *);

#endif
