#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define ESP_OK 0
#define ESP_FAIL -1
#define ERR_OK 0
#define ERR_MEM -1
#define unlikely(x) (x)
#define ESP_LOGE(...) ((void)0)
typedef int err_t;
struct pbuf {
    bool allocated;
    uint16_t len, tot_len;
    void *payload;
    struct pbuf *next;
};
struct netif {
    bool up;
    void *state;
    err_t (*input)(struct pbuf *, struct netif *);
};
#define netif_is_up(n) ((n)->up)
#if CONFIG_ESP_NETIF_RECEIVE_REPORT_ERRORS
typedef int esp_netif_recv_ret_t;
#define ESP_NETIF_OPTIONAL_RETURN_CODE(code) (code)
#else
typedef void esp_netif_recv_ret_t;
#define ESP_NETIF_OPTIONAL_RETURN_CODE(code)
#endif
unsigned pbuf_free(struct pbuf *p);
uint16_t pbuf_copy_partial(const struct pbuf *p, void *dst, uint16_t len, uint16_t offset);
void esp_netif_free_rx_buffer(void *state, void *buffer);
esp_netif_recv_ret_t ring_link_rx_netstack_lwip_input_fn(
    void *h, void *buffer, size_t len, void *l2_buff);
