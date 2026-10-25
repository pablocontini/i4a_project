#include "wireless/wireless.h"
#include "node.h"

void wl_register_peer_callbacks(wireless_t *wl, wireless_callbacks_t callbacks, void *context) {
    wl->callbacks = callbacks;
    wl->context = context;
}

bool wl_send_peer_message(wireless_t *wl, const void *msg, uint16_t len){
    return node_send_wireless_message(msg, len);
}

bool wl_get_provision_offer(wireless_t *wl, uint32_t *network, uint32_t *mask) {
    (void)wl;
    return node_get_peer_provision_offer(network, mask);
}

bool wl_check_peer_network(wireless_t *wl, uint32_t network, uint32_t mask) {
    (void)wl;
    return node_check_peer_network(network, mask);
}

void wl_enable_ap_mode(wireless_t *wl, uint32_t network, uint32_t mask){
    return node_set_as_ap(network, mask);
}
