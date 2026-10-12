#include <stdio.h>
#include "device.h"
#include "routing_hooks.h"
#include "node.h"
#include "ip_diagnostics.h"
#define CHECK(value) do { if (!(value)) return __LINE__; } while (0)
static Device device;
static AccessPoint ap;
static Station station;
static struct netif wifi, spi;
static esp_netif_t wifi_handle = {&wifi}, spi_handle = {&spi};
void *esp_netif_get_netif_impl(esp_netif_t *handle) { return handle->impl; }
esp_netif_t *node_get_wifi_netif(void) { return &wifi_handle; }
esp_netif_t *node_get_spi_netif(void) { return &spi_handle; }
bool node_is_point_to_point_message(uint32_t dst) { return device_is_point_to_point_message(&device, dst); }
bool node_is_packet_for_this_subnet(uint32_t dst) { (void)dst; return false; }
rt_routing_result_t rt_do_route(routing_t *rt, uint32_t src, uint32_t dst) {
    (void)rt; (void)src; (void)dst; return ROUTE_SPI;
}
void routing_table_show(const rt_routing_table_t *table) { (void)table; }
void ip_diag_route(uint32_t src, uint32_t dst, ip_diag_route_t decision) {
    (void)src; (void)dst; (void)decision;
}
static err_t output(struct netif *netif, struct pbuf *p, const ip4_addr_t *dst) {
    (void)netif; (void)p; (void)dst; return ERR_OK;
}
static void reset(void) {
    ap = (AccessPoint){0}; station = (Station){0};
    device = (Device){.mode=STATION, .access_point_ptr=&ap, .station_ptr=&station};
    wifi.flags = 0; wifi.output = output;
    spi.flags = NETIF_FLAG_UP | NETIF_FLAG_LINK_UP; spi.output = output;
    node_set_routing_hook(ROUTING_HOOK_FORWARDER);
}
static int search_without_ip_must_not_intercept_transit(void) {
    reset();
    CHECK(node_do_routing(0x0A940002, 0x0A800002) == &spi);
    CHECK(node_do_routing(0x0A940002, 0x0AFFFFFE) == &spi);
    CHECK(node_do_routing(0x0A940002, 0x01010101) == &spi);
    station.is_fully_connected = true; /* An invalid mask is still unusable. */
    CHECK(!device_is_point_to_point_message(&device, 0x01010101));
    return 0;
}
static int connected_station_preserves_transport_only(void) {
    reset();
    station.sta_subnet=0x0A800000; station.sta_mask=0xFFFFFFFC;
    station.is_fully_connected=true;
    wifi.flags=NETIF_FLAG_UP | NETIF_FLAG_LINK_UP;
    CHECK(device_is_point_to_point_message(&device,0x0A800001));
    CHECK(device_is_point_to_point_message(&device,0x0A800002));
    CHECK(node_do_routing(0x0A940002,0x0A800001)==&wifi);
    CHECK(node_do_routing(0x0A940002,0x0AFFFFFE)==&spi);
    CHECK(node_do_routing(0x0A940002,0x01010101)==&spi);
    return 0;
}
static int inactive_or_stopping_transport_is_not_a_shortcut(void) {
    reset();
    station.sta_subnet=0x0A800000; station.sta_mask=0xFFFFFFFC;
    CHECK(!device_is_point_to_point_message(&device,0x0A800001));
    station.is_fully_connected=true; station.stopping=true;
    CHECK(!device_is_point_to_point_message(&device,0x0A800001));
    device.mode=AP;
    CHECK(!device_is_point_to_point_message(&device,0x01010101));
    ap.ap_subnet=0x0A880000; ap.ap_mask=0xFFFC0000;
    CHECK(device_is_point_to_point_message(&device,0x0A880001));
    CHECK(!device_is_point_to_point_message(&device,0x0A800001));
    ap.stopping=true;
    CHECK(!device_is_point_to_point_message(&device,0x0A880001));
    return 0;
}
static int absent_device_and_modes_are_safe(void) {
    reset();
    CHECK(!device_is_point_to_point_message(NULL,0x01010101));
    device.station_ptr=NULL;
    CHECK(!device_is_point_to_point_message(&device,0x01010101));
    device.mode=AP; device.access_point_ptr=NULL;
    CHECK(!device_is_point_to_point_message(&device,0x01010101));
    device.mode=NAN;
    CHECK(!device_is_point_to_point_message(&device,0x01010101));
    return 0;
}
#define RUN(test) do { int line=test(); if(line){printf("%s: FAIL line %d\n",#test,line);return 1;} printf("%s: OK\n",#test); } while(0)
int main(void) {
    RUN(search_without_ip_must_not_intercept_transit);
    RUN(connected_station_preserves_transport_only);
    RUN(inactive_or_stopping_transport_is_not_a_shortcut);
    RUN(absent_device_and_modes_are_safe);
    return 0;
}
