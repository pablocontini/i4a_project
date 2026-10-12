#ifndef I4A_SERVER_TEST_ESP_NETIF_H
#define I4A_SERVER_TEST_ESP_NETIF_H
#include <stdint.h>
#include <stdlib.h>
typedef void esp_netif_t;
typedef struct {struct {uint32_t addr;} ip,netmask,gw;} esp_netif_ip_info_t;
#define ESP_OK 0
#define ESP_ERR_NO_MEM 1
#define ESP_ERROR_CHECK(x) do {if ((x)!=0) abort();} while(0)
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key);
int esp_netif_get_ip_info(esp_netif_t *netif,esp_netif_ip_info_t *info);

#endif
