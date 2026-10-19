#include "ring_link_netif_rx.h"
#include <stdio.h>
#define CHECK(expr) do { if (!(expr)) return __LINE__; } while (0)

static unsigned freed, consumed, calls;
static bool reject, bad_free;
unsigned pbuf_free(struct pbuf *p) {
    if (!p || !p->allocated) { bad_free = true; return 0; }
    p->allocated = false;
    ++freed;
    return 1;
}
void esp_netif_free_rx_buffer(void *state, void *buffer) {
    (void)state; (void)buffer;
    bad_free = true; /* This interface never hands in an L2 buffer. */
}
err_t tcpip_input_mock(struct pbuf *p, struct netif *netif) {
    (void)netif;
    ++calls;
    if (reject) return ERR_MEM;
    /* Model the successful handoff being consumed by the TCP/IP worker. */
    p->allocated = false;
    ++consumed;
    return ERR_OK;
}
/* Model esp_netif_receive in both supported ESP-IDF configurations. The
 * caller hands off ownership; it cannot infer rejection when errors are off. */
int receive(struct netif *netif, struct pbuf *p) {
#if CONFIG_ESP_NETIF_RECEIVE_REPORT_ERRORS
    return ring_link_rx_netstack_lwip_input_fn(netif, p, sizeof(*p), NULL);
#else
    ring_link_rx_netstack_lwip_input_fn(netif, p, sizeof(*p), NULL);
    return ESP_OK;
#endif
}
void reset(void) { freed = consumed = calls = 0; bad_free = reject = false; }

int test_saturated_mailbox_reclaims_every_rejected_pbuf(void) {
    reset();
    struct netif netif = { .up = true };
    netif.input = tcpip_input_mock;
    for (unsigned i = 0; i < 10000; ++i) {
        struct pbuf p = { .allocated = true };
        reject = i % 4 != 0;
        int result = receive(&netif, &p);
#if CONFIG_ESP_NETIF_RECEIVE_REPORT_ERRORS
        CHECK(result == (reject ? ESP_FAIL : ESP_OK));
#else
        CHECK(result == ESP_OK);
#endif
        CHECK(!p.allocated && !bad_free);
    }
    CHECK(calls == 10000 && freed == 7500 && consumed == 2500);
    return 0;
}

int test_interface_down_reclaims_without_handoff(void) {
    reset();
    struct netif netif = { .up = false };
    netif.input = tcpip_input_mock;
    struct pbuf p = { .allocated = true };
    receive(&netif, &p);
    CHECK(!p.allocated && freed == 1 && !calls && !bad_free);
    return 0;
}

int test_invalid_interface_and_missing_input_reclaim_safely(void) {
    reset();
    struct pbuf p = { .allocated = true };
    receive(NULL, &p);
    CHECK(!p.allocated && freed == 1 && !bad_free);
    p.allocated = true;
    struct netif netif = { .up = true, .input = NULL };
    receive(&netif, &p);
    CHECK(!p.allocated && freed == 2 && !calls && !bad_free);
    receive(&netif, NULL);
    CHECK(freed == 2 && !bad_free);
    return 0;
}

#define RUN(test) do { \
    int line = test(); \
    if (line) { \
        printf("%s (report_errors=%d): FAILED input_test.c:%d\n", \
               #test, CONFIG_ESP_NETIF_RECEIVE_REPORT_ERRORS, line); \
        return 1; \
    } \
    printf("%s (report_errors=%d): OK\n", #test, CONFIG_ESP_NETIF_RECEIVE_REPORT_ERRORS); \
} while (0)

int main(void) {
    RUN(test_saturated_mailbox_reclaims_every_rejected_pbuf);
    RUN(test_interface_down_reclaims_without_handoff);
    RUN(test_invalid_interface_and_missing_input_reclaim_safely);
    return 0;
}
