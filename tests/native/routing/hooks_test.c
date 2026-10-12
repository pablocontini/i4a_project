#include "routing_hooks.h"
#include "node.h"
#include "ip_diagnostics.h"

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif
#define CHECK(expr) do { if (!(expr)) return __LINE__; } while (0)
static rt_routing_result_t route;
static bool point_to_point;
static unsigned outputs;
static struct netif wifi, spi;
static esp_netif_t wifi_handle = {&wifi}, spi_handle = {&spi};
static ip_diag_route_t last_decision;
static uint32_t last_diagnostic_destination;
void ip_diag_route(uint32_t src, uint32_t dst, ip_diag_route_t decision) {
    (void)src; last_decision = decision; last_diagnostic_destination = dst;
}

void *esp_netif_get_netif_impl(esp_netif_t *handle) { return handle->impl; }
esp_netif_t *node_get_wifi_netif(void) { return &wifi_handle; }
esp_netif_t *node_get_spi_netif(void) { return &spi_handle; }
bool node_is_point_to_point_message(uint32_t dst) { (void)dst; return point_to_point; }
bool node_is_packet_for_this_subnet(uint32_t dst) { (void)dst; return false; }
rt_routing_result_t rt_do_route(routing_t *rt, uint32_t src, uint32_t dst) {
    (void)rt; (void)src; (void)dst; return route;
}
void routing_table_show(const rt_routing_table_t *table) { (void)table; }
static err_t normal_output(struct netif *netif, struct pbuf *p, const ip4_addr_t *dst) {
    (void)netif; (void)p; (void)dst; ++outputs; return ERR_OK;
}

EXPORT int test_no_route_hook_blocks_default_fallback_without_freeing_packet(void) {
    route = ROUTE_NONE;
    point_to_point = false;
    outputs = 0;
    wifi.flags = spi.flags = NETIF_FLAG_UP | NETIF_FLAG_LINK_UP;
    wifi.output = spi.output = normal_output;
    node_set_routing_hook(ROUTING_HOOK_FORWARDER);
    struct pbuf packet = {.len = 123};
    ip4_addr_t destination = {0x01010101};
    struct netif *result = node_do_routing(0, destination.addr);
    CHECK(result != NULL && result != &spi && result != &wifi);
    CHECK(last_decision == IP_DIAG_ROUTE_SINK && last_diagnostic_destination == destination.addr);
    CHECK(result->output(result, &packet, &destination) == ERR_RTE);
    CHECK(packet.len == 123 && outputs == 0);
    route = ROUTE_SPI;
    CHECK(node_do_routing(0, destination.addr) == &spi);
    CHECK(last_decision == IP_DIAG_ROUTE_SPI);
    route = ROUTE_WIFI;
    CHECK(node_do_routing(0, destination.addr) == &wifi);
    CHECK(last_decision == IP_DIAG_ROUTE_WIFI);
    wifi.flags = 0;
    result = node_do_routing(0, destination.addr);
    CHECK(result != NULL && result != &spi && result != &wifi);
    CHECK(result->output(result, &packet, &destination) == ERR_RTE);
    point_to_point = true;
    CHECK(node_do_routing(0, destination.addr) == result);
    point_to_point = false;
    route = ROUTE_SPI;
    spi.output = NULL;
    CHECK(node_do_routing(0, destination.addr) == result);
    return 0;
}
