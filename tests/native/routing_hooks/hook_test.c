#include <stdio.h>
/* Supply a small native model of the public ESP/routing types. */
#define _ROUTING_HOOKS_H_
#include "node.h"
#include "../../../components/integration/routing_hooks/routing_hooks.c"
#define CHECK(expr) do { if (!(expr)) return __LINE__; } while (0)
static struct netif wifi_impl, spi_impl;
static esp_netif_t wifi, spi;
static bool point_to_point, local_subnet, wifi_available, spi_available;
static rt_routing_result_t route;
static unsigned wifi_packets, spi_packets;
static err_t wifi_output(struct netif *n, struct pbuf *p, const ip4_addr_t *d) {
    (void)n; (void)p; (void)d; ++wifi_packets; return ERR_OK;
}
static err_t spi_output(struct netif *n, struct pbuf *p, const ip4_addr_t *d) {
    (void)n; (void)p; (void)d; ++spi_packets; return ERR_OK;
}
bool node_is_point_to_point_message(uint32_t d) { (void)d; return point_to_point; }
bool node_is_packet_for_this_subnet(uint32_t d) { (void)d; return local_subnet; }
esp_netif_t *node_get_wifi_netif(void) { return wifi_available ? &wifi : NULL; }
esp_netif_t *node_get_spi_netif(void) { return spi_available ? &spi : NULL; }
struct netif *esp_netif_get_netif_impl(esp_netif_t *h) { return h->impl; }
rt_routing_result_t rt_do_route(routing_t *r, uint32_t s, uint32_t d) {
    (void)r; (void)s; (void)d; return route;
}
void routing_table_show(int *t) { (void)t; }
static void reset(void) {
    wifi_impl = (struct netif){ .flags = NETIF_FLAG_UP, .output = wifi_output };
    spi_impl = (struct netif){ .flags = NETIF_FLAG_UP, .output = spi_output };
    wifi.impl = &wifi_impl; spi.impl = &spi_impl;
    wifi_available = spi_available = true;
    point_to_point = local_subnet = false;
    route = ROUTE_NONE;
    wifi_packets = spi_packets = 0;
    user_defined_hook = NULL;
    node_set_routing_hook(ROUTING_HOOK_FORWARDER);
}
/* Model lwIP's hook contract: a NULL result permits default-route fallback. */
static err_t send(void) {
    struct netif *selected = node_do_routing(1, 2);
    if (!selected) selected = &spi_impl;
    return selected->output(selected, NULL, NULL);
}
int test_no_route_never_falls_back_to_default_spi(void) {
    reset();
    CHECK(send() == ERR_RTE);
    CHECK(node_do_routing(1, 2) && !spi_packets && !wifi_packets);
    return 0;
}
int test_selected_interface_unavailable_discards_without_fallback(void) {
    reset();
    route = ROUTE_WIFI;
    wifi_available = false;
    CHECK(send() == ERR_RTE && !spi_packets);
    wifi_available = true; wifi.impl = NULL;
    CHECK(send() == ERR_RTE && !spi_packets);
    wifi.impl = &wifi_impl; wifi_impl.flags = 0;
    CHECK(send() == ERR_RTE && !spi_packets);
    wifi_impl.flags = NETIF_FLAG_UP; wifi_impl.output = NULL;
    CHECK(send() == ERR_RTE && !spi_packets);
    route = ROUTE_SPI; spi_available = false;
    CHECK(send() == ERR_RTE && !spi_packets);
    return 0;
}
int test_explicit_wifi_and_spi_routes_still_transmit(void) {
    reset();
    route = ROUTE_WIFI;
    CHECK(send() == ERR_OK && wifi_packets == 1 && !spi_packets);
    route = ROUTE_SPI;
    CHECK(send() == ERR_OK && spi_packets == 1 && wifi_packets == 1);
    point_to_point = true;
    CHECK(send() == ERR_OK && wifi_packets == 2);
    return 0;
}
static struct netif *custom_fallback(uint32_t s, uint32_t d) { (void)s; (void)d; return NULL; }
int test_normal_wifi_and_custom_hooks_preserve_lwip_fallback(void) {
    reset();
    node_set_routing_hook(ROUTING_HOOK_WIFI_NETIF_DEFAULT);
    CHECK(node_do_routing(1, 2) == NULL);
    node_register_custom_routing_hook(custom_fallback);
    node_set_routing_hook(ROUTING_HOOK_CUSTOM);
    CHECK(node_do_routing(1, 2) == NULL);
    return 0;
}
int test_home_and_root_interface_loss_discard(void) {
    reset();
    node_set_routing_hook(ROUTING_HOOK_ROOT_CENTER);
    wifi_available = false;
    CHECK(send() == ERR_RTE && !spi_packets);
    local_subnet = true;
    CHECK(send() == ERR_OK && spi_packets == 1);
    node_set_routing_hook(ROUTING_HOOK_HOME);
    CHECK(send() == ERR_RTE && spi_packets == 1);
    local_subnet = false;
    CHECK(send() == ERR_OK && spi_packets == 2);
    return 0;
}
#define RUN(test) do { int line = test(); if (line) { \
    printf("%s: FAILED hook_test.c:%d\n", #test, line); return 1; } \
    printf("%s: OK\n", #test); } while (0)
int main(void) {
    RUN(test_no_route_never_falls_back_to_default_spi);
    RUN(test_selected_interface_unavailable_discards_without_fallback);
    RUN(test_explicit_wifi_and_spi_routes_still_transmit);
    RUN(test_normal_wifi_and_custom_hooks_preserve_lwip_fallback);
    RUN(test_home_and_root_interface_loss_discard);
    return 0;
}
