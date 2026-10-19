#include "ring_link_netif_rx.h"

esp_netif_recv_ret_t ring_link_rx_netstack_lwip_input_fn(
    void *h, void *buffer, size_t len, void *l2_buff)
{
    struct netif *netif = h;
    struct pbuf *p = buffer;
    (void)len;
    /* This custom interface receives an allocated pbuf, not an L2 DMA buffer. */
    (void)l2_buff;

    if (!p) return ESP_NETIF_OPTIONAL_RETURN_CODE(ESP_FAIL);
    if (!netif || !netif_is_up(netif) || !netif->input) {
        pbuf_free(p);
        return ESP_NETIF_OPTIONAL_RETURN_CODE(ESP_FAIL);
    }

    /* tcpip_input takes ownership only on ERR_OK. Free rejections here:
     * esp_netif_receive returns ESP_OK unconditionally in the default IDF
     * configuration, so its caller cannot detect a full TCP/IP mailbox. */
    if (netif->input(p, netif) != ERR_OK) {
        pbuf_free(p);
        return ESP_NETIF_OPTIONAL_RETURN_CODE(ESP_FAIL);
    }
    return ESP_NETIF_OPTIONAL_RETURN_CODE(ESP_OK);
}
