/* Compile the complete production driver, exercising its installed callbacks. */
#include "ip_diagnostics.h"
#if TEST_TX
#include "../../../components/internal/ring_link_netif/ring_link_netif_tx.c"
#define INIT ring_link_tx_netstack_init_fn
#define DRIVER "TX"
#else
#include "../../../components/internal/ring_link_netif/ring_link_netif_rx.c"
#define INIT ring_link_rx_netstack_lwip_init_fn
#define DRIVER "RX"
esp_netif_recv_ret_t ring_link_rx_netstack_lwip_input_fn(void *h, void *b, size_t s, void *l) {
    (void)h; (void)b; (void)s; (void)l; return ESP_OK;
}
#endif

#define CHECK(expr) do { if (!(expr)) { printf("%s:%d: %s\n", DRIVER, __LINE__, #expr); return 1; } } while (0)
static esp_netif_t handle;
static esp_err_t transmit_result;
static err_t output_result;
static unsigned transmit_calls, output_calls, frees, allocations;
static int fail_alloc, missing_handle;
static uint8_t allocation_buffer[1500], transmitted[1500];
static size_t transmitted_len;
static struct pbuf allocation;

/* Diagnostics observe packets only. They never substitute a TX result. */
void ip_diag_packet(const void *data, size_t captured_len,
                    ip_diag_stage_t stage, int error) {
    (void)data; (void)captured_len; (void)stage; (void)error;
}
void ip_diag_pbuf(const struct pbuf *p, ip_diag_stage_t stage, int error) {
    (void)p; (void)stage; (void)error;
}
void ip_diag_route(uint32_t src, uint32_t dst, ip_diag_route_t decision) {
    (void)src; (void)dst; (void)decision;
}

esp_netif_t *esp_netif_get_handle_from_netif_impl(struct netif *n) {
    (void)n; return missing_handle ? NULL : &handle;
}
esp_err_t esp_netif_transmit(esp_netif_t *n, void *buffer, size_t len) {
    assert(n == &handle);
    ++transmit_calls;
    memcpy(transmitted, buffer, len);
    transmitted_len = len;
    return transmit_result;
}
struct pbuf *pbuf_alloc(int layer, u16_t len, int kind) {
    (void)layer; (void)kind;
    ++allocations;
    if (fail_alloc) return NULL;
    allocation.payload = allocation_buffer;
    allocation.len = allocation.tot_len = len;
    allocation.next = NULL;
    return &allocation;
}
err_t pbuf_copy(struct pbuf *destination, const struct pbuf *source) {
    size_t offset = 0;
    for (; source; source = source->next) {
        memcpy((uint8_t *)destination->payload + offset, source->payload, source->len);
        offset += source->len;
    }
    return ERR_OK;
}
unsigned pbuf_free(struct pbuf *p) { assert(p == &allocation); ++frees; return 1; }
static err_t faulting_linkoutput(struct netif *n, struct pbuf *p) {
    (void)n; (void)p; ++output_calls; return output_result;
}
static void reset(void) {
    transmit_calls = output_calls = frees = allocations = 0;
    fail_alloc = missing_handle = 0;
    transmit_result = ESP_OK;
    output_result = ERR_OK;
    transmitted_len = 0;
}

static int test_output_preserves_callback_errors_for_ipv4_and_ipv6(void) {
    struct netif n = {0};
    uint8_t bytes[40] = {0};
    struct pbuf p = { .payload = bytes, .len = sizeof(bytes), .tot_len = sizeof(bytes) };
    INIT(&n);
    n.linkoutput = faulting_linkoutput;
    const err_t errors[] = { ERR_OK, ERR_MEM, ERR_IF, ERR_VAL };
    for (unsigned v = 4; v <= 6; v += 2) {
        bytes[0] = v << 4;
        for (unsigned i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
            reset(); output_result = errors[i];
            CHECK(n.output(&n, &p, NULL) == errors[i]);
            CHECK(output_calls == 1 && transmit_calls == 0);
        }
    }
    return 0;
}
static int test_invalid_frames_never_reach_linkoutput(void) {
    struct netif n = {0};
    uint8_t bytes[40] = {0};
    struct pbuf p = { .payload = bytes, .len = sizeof(bytes), .tot_len = sizeof(bytes) };
    INIT(&n); n.linkoutput = faulting_linkoutput;
    reset();
    for (unsigned v = 0; v < 16; ++v) {
        if (v == 4 || v == 6) continue;
        bytes[0] = v << 4;
        CHECK(n.output(&n, &p, NULL) == ERR_VAL);
    }
    CHECK(n.output(&n, NULL, NULL) == ERR_VAL);
    p.payload = NULL;
    CHECK(n.output(&n, &p, NULL) == ERR_VAL);
    p.payload = bytes; p.len = 0;
    CHECK(n.output(&n, &p, NULL) == ERR_VAL);
    CHECK(output_calls == 0);
    CHECK(n.output(NULL, &p, NULL) == ERR_IF);
    n.linkoutput = NULL;
    CHECK(n.output(&n, &p, NULL) == ERR_IF);
    return 0;
}
static int test_real_driver_reports_failed_spi_handoff(void) {
    struct netif n = {0};
    uint8_t bytes[40] = {0x45};
    struct pbuf p = { .payload = bytes, .len = sizeof(bytes), .tot_len = sizeof(bytes) };
    INIT(&n);
    const esp_err_t errors[] = { ESP_OK, ESP_ERR_NO_MEM, ESP_FAIL };
    const err_t expected[] = { ERR_OK, ERR_MEM, ERR_IF };
    for (unsigned i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        reset(); transmit_result = errors[i];
        CHECK(n.output(&n, &p, NULL) == expected[i]);
        CHECK(transmit_calls == 1 && transmitted_len == sizeof(bytes));
        CHECK(memcmp(transmitted, bytes, sizeof(bytes)) == 0);
        CHECK(frees == 0 && allocations == 0);
    }
    reset(); missing_handle = 1;
    CHECK(n.output(&n, &p, NULL) == ERR_IF && transmit_calls == 0);
    return 0;
}
static int test_chained_packets_keep_error_and_free_temporary_copy(void) {
    struct netif n = {0};
    uint8_t first[20] = {0x45}, second[20] = {0x7f};
    struct pbuf tail = { .payload = second, .len = sizeof(second), .tot_len = sizeof(second) };
    struct pbuf p = { .payload = first, .len = sizeof(first), .tot_len = 40, .next = &tail };
    INIT(&n);
    reset(); transmit_result = ESP_FAIL;
    CHECK(n.output(&n, &p, NULL) == ERR_IF);
    CHECK(transmit_calls == 1 && frees == 1 && allocations == 1 && transmitted_len == 40);
    CHECK(memcmp(transmitted, first, 20) == 0 && memcmp(transmitted + 20, second, 20) == 0);
    CHECK(p.next == &tail && p.payload == first && tail.payload == second);
    reset(); fail_alloc = 1;
    CHECK(n.output(&n, &p, NULL) == ERR_MEM);
    CHECK(transmit_calls == 0 && frees == 0 && allocations == 1);
    return 0;
}
#define RUN(test) do { if (test()) return 1; printf("%s %s: OK\n", DRIVER, #test); } while (0)
int main(void) {
    RUN(test_output_preserves_callback_errors_for_ipv4_and_ipv6);
    RUN(test_invalid_frames_never_reach_linkoutput);
    RUN(test_real_driver_reports_failed_spi_handoff);
    RUN(test_chained_packets_keep_error_and_free_temporary_copy);
    return 0;
}
