#ifndef NATIVE_ROUTING_HOOKS_NODE_H
#define NATIVE_ROUTING_HOOKS_NODE_H
#include <stdbool.h>
#include <stdint.h>
#include "lwip/netif.h"
typedef struct { struct netif *impl; } esp_netif_t;
typedef struct { int routing_table; } node_state_t;
typedef struct { node_state_t node_state; } routing_t;
typedef enum { ROUTE_NONE, ROUTE_WIFI, ROUTE_SPI } rt_routing_result_t;
typedef enum {
    ROUTING_HOOK_ROOT_CENTER, ROUTING_HOOK_FORWARDER, ROUTING_HOOK_HOME,
    ROUTING_HOOK_WIFI_NETIF_DEFAULT, ROUTING_HOOK_CUSTOM, ROUTING_HOOK_COUNT
} routing_hook_type_t;
typedef struct netif *(*routing_hook_func_t)(uint32_t, uint32_t);
bool node_is_point_to_point_message(uint32_t destination);
bool node_is_packet_for_this_subnet(uint32_t destination);
esp_netif_t *node_get_wifi_netif(void);
esp_netif_t *node_get_spi_netif(void);
struct netif *esp_netif_get_netif_impl(esp_netif_t *handle);
rt_routing_result_t rt_do_route(routing_t *rt, uint32_t source, uint32_t destination);
void routing_table_show(int *table);
#endif
