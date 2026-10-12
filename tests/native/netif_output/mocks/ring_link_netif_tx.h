#ifndef I4A_TEST_NETIF_OUTPUT_TX_H
#define I4A_TEST_NETIF_OUTPUT_TX_H
#include "netif_output_common.h"
err_t ring_link_tx_netstack_init_fn(struct netif *netif);
esp_netif_recv_ret_t ring_link_tx_netstack_input_fn(void *, void *, size_t, void *);
#endif
