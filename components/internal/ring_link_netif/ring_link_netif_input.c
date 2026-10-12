#include "ring_link_netif_rx.h"
#include "ip_diagnostics.h"

esp_netif_recv_ret_t ring_link_rx_netstack_lwip_input_fn(
    void *h, void *buffer, size_t len, void *l2_buff)
{
    struct netif *netif = h;
    struct pbuf *p = buffer;
    (void)len;
    /* This custom interface receives an allocated pbuf, not an L2 DMA buffer. */
    (void)l2_buff;

    if (!p) return ESP_NETIF_OPTIONAL_RETURN_CODE(ESP_FAIL);
    /* Snapshot before tcpip_input: successful handoff can consume/free p
     * immediately. A chained pbuf need not contain an entire header in its
     * first segment. Diagnostics never retain the pbuf pointer. */
    uint8_t prefix[68];
    uint16_t prefix_len = p->tot_len < sizeof(prefix) ? p->tot_len : sizeof(prefix);
    uint16_t copied = pbuf_copy_partial(p, prefix, prefix_len, 0);
    ip_diag_packet(prefix, copied, IP_DIAG_SPI_INPUT, 0);
    if (!netif || !netif_is_up(netif) || !netif->input) {
        ip_diag_packet(prefix, copied, IP_DIAG_SPI_INPUT_REJECTED, ESP_FAIL);
        pbuf_free(p);
        return ESP_NETIF_OPTIONAL_RETURN_CODE(ESP_FAIL);
    }

    /* tcpip_input takes ownership only on ERR_OK. Free rejections here:
     * esp_netif_receive returns ESP_OK unconditionally in the default IDF
     * configuration, so its caller cannot detect a full TCP/IP mailbox. */
    err_t error = netif->input(p, netif);
    if (error != ERR_OK) {
        ip_diag_packet(prefix, copied, IP_DIAG_SPI_INPUT_REJECTED, error);
        pbuf_free(p);
        return ESP_NETIF_OPTIONAL_RETURN_CODE(ESP_FAIL);
    }
    ip_diag_packet(prefix, copied, IP_DIAG_SPI_INPUT_ACCEPTED, 0);
    return ESP_NETIF_OPTIONAL_RETURN_CODE(ESP_OK);
}
