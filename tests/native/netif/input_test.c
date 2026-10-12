#include "ring_link_netif_rx.h"
#include <stdio.h>
#include <string.h>
#include "ip_diagnostics.h"
#define CHECK(expr) do { if (!(expr)) return __LINE__; } while (0)

static unsigned freed, consumed, calls;
static bool reject, bad_free;
static unsigned diagnostic_calls[IP_DIAG_STAGE_COUNT];
static uint8_t diagnostic_prefix[IP_DIAG_STAGE_COUNT][68];
static size_t diagnostic_len[IP_DIAG_STAGE_COUNT];
static int diagnostic_error[IP_DIAG_STAGE_COUNT];
void ip_diag_packet(const void *data, size_t len, ip_diag_stage_t stage, int error) {
    ++diagnostic_calls[stage];
    diagnostic_len[stage] = len;
    diagnostic_error[stage] = error;
    if (len) memcpy(diagnostic_prefix[stage], data, len);
}
uint16_t pbuf_copy_partial(const struct pbuf *p, void *dst, uint16_t len, uint16_t offset) {
    uint16_t copied = 0;
    for (; p && copied < len; p = p->next) {
        if (offset >= p->len) { offset -= p->len; continue; }
        uint16_t part = p->len - offset;
        if (part > len - copied) part = len - copied;
        memcpy((uint8_t *)dst + copied, (uint8_t *)p->payload + offset, part);
        copied += part; offset = 0;
    }
    return copied;
}
unsigned pbuf_free(struct pbuf *p) {
    if (!p || !p->allocated) { bad_free = true; return 0; }
    p->allocated = false;
    if (p->payload) memset(p->payload, 0xee, p->len);
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
    if (p->payload) memset(p->payload, 0xee, p->len);
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
void reset(void) {
    freed = consumed = calls = 0; bad_free = reject = false;
    memset(diagnostic_calls, 0, sizeof(diagnostic_calls));
    memset(diagnostic_len, 0, sizeof(diagnostic_len));
}

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

int test_successful_handoff_uses_snapshot_after_consumption(void) {
    reset();
    uint8_t bytes[28] = {0x45, 0, 0, 28};
    bytes[9] = 1; bytes[20] = 8; bytes[27] = 19;
    uint8_t original[28]; memcpy(original, bytes, sizeof(bytes));
    struct pbuf p = { .allocated = true, .len = 28, .tot_len = 28, .payload = bytes };
    struct netif netif = { .up = true, .input = tcpip_input_mock };
    receive(&netif, &p);
    CHECK(bytes[0] == 0xee && !p.allocated);
    CHECK(diagnostic_calls[IP_DIAG_SPI_INPUT] == 1);
    CHECK(diagnostic_calls[IP_DIAG_SPI_INPUT_ACCEPTED] == 1);
    CHECK(!diagnostic_calls[IP_DIAG_SPI_INPUT_REJECTED]);
    CHECK(diagnostic_len[IP_DIAG_SPI_INPUT_ACCEPTED] == sizeof(original));
    CHECK(!memcmp(diagnostic_prefix[IP_DIAG_SPI_INPUT_ACCEPTED], original, sizeof(original)));
    return 0;
}

int test_chained_rejection_preserves_header_and_error(void) {
    reset(); reject = true;
    uint8_t bytes[28] = {0x45, 0, 0, 28};
    bytes[9] = 1; bytes[20] = 8; bytes[27] = 23;
    uint8_t original[28]; memcpy(original, bytes, sizeof(bytes));
    struct pbuf tail = { .allocated = true, .len = 24, .tot_len = 24, .payload = bytes + 4 };
    struct pbuf p = { .allocated = true, .len = 4, .tot_len = 28, .payload = bytes, .next = &tail };
    struct netif netif = { .up = true, .input = tcpip_input_mock };
    receive(&netif, &p);
    CHECK(freed == 1 && !consumed && !bad_free);
    CHECK(diagnostic_calls[IP_DIAG_SPI_INPUT_REJECTED] == 1);
    CHECK(diagnostic_error[IP_DIAG_SPI_INPUT_REJECTED] == ERR_MEM);
    CHECK(!memcmp(diagnostic_prefix[IP_DIAG_SPI_INPUT_REJECTED], original, sizeof(original)));
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
    RUN(test_successful_handoff_uses_snapshot_after_consumption);
    RUN(test_chained_rejection_preserves_header_and_error);
    return 0;
}
