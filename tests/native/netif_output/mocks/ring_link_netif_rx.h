#ifndef I4A_TEST_NETIF_OUTPUT_RX_H
#define I4A_TEST_NETIF_OUTPUT_RX_H
#include "netif_output_common.h"
err_t ring_link_rx_netstack_lwip_init_fn(struct netif *netif);
esp_netif_recv_ret_t ring_link_rx_netstack_lwip_input_fn(void *, void *, size_t, void *);
#endif
