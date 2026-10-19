#include <stdio.h>
#include <stdlib.h>
#include "ring_link_netif_common.h"

static unsigned allocations, frees, transmissions;
static bool allocation_fails;
static esp_err_t transmit_result;
static ring_link_payload_t sent;

static void *tracked_calloc(size_t n, size_t size) {
    if (allocation_fails) return NULL;
    ++allocations;
    return calloc(n, size);
}
static void tracked_free(void *p) { ++frees; free(p); }
#define calloc tracked_calloc
#define free tracked_free
#include "../../../components/internal/ring_link_netif/ring_link_netif_payload.c"
#undef calloc
#undef free

#define CHECK(expr) do { if (!(expr)) return __LINE__; } while (0)
uint8_t config_get_id(void) { return 3; }
esp_err_t ring_link_lowlevel_transmit_payload(ring_link_payload_t *payload) {
    ++transmissions;
    sent = *payload;
    return transmit_result;
}
static void reset(void) {
    allocations = frees = transmissions = 0;
    allocation_fails = false;
    transmit_result = ESP_OK;
}
static ring_link_payload_t ipv4(unsigned length) {
    ring_link_payload_t p = { .len = length };
    p.buffer[0] = 0x45;
    p.buffer[2] = (char)(length >> 8);
    p.buffer[3] = (char)length;
    return p;
}

int test_ip_bounds_reject_invalid_header_and_oversized_frame(void) {
    uint16_t length = 0;
    ring_link_payload_t p = ipv4(20);
    CHECK(ring_link_netif_ip_length(&p, &length) && length == 20);
    CHECK(!ring_link_netif_ip_length(NULL, &length));
    CHECK(!ring_link_netif_ip_length(&p, NULL));
    p.len = RING_LINK_NETIF_MTU + 1;
    CHECK(!ring_link_netif_ip_length(&p, &length));
    p = ipv4(19);
    CHECK(!ring_link_netif_ip_length(&p, &length));
    p = ipv4(20); p.buffer[0] = 0x44; /* IHL below minimum. */
    CHECK(!ring_link_netif_ip_length(&p, &length));
    p.buffer[0] = 0x46; /* Options extend beyond the received bytes. */
    CHECK(!ring_link_netif_ip_length(&p, &length));
    p = ipv4(20); p.buffer[3] = 0;
    CHECK(!ring_link_netif_ip_length(&p, &length));
    p.buffer[3] = 21;
    CHECK(!ring_link_netif_ip_length(&p, &length));
    p = ipv4(RING_LINK_NETIF_MTU);
    CHECK(ring_link_netif_ip_length(&p, &length) && length == RING_LINK_NETIF_MTU);
    p.buffer[0] = 0x15;
    CHECK(!ring_link_netif_ip_length(&p, &length));
    return 0;
}

int test_ipv6_uses_payload_length_and_fixed_header(void) {
    uint16_t length = 0;
    ring_link_payload_t p = { .len = 60 };
    p.buffer[0] = 0x60;
    p.buffer[5] = 20;
    CHECK(ring_link_netif_ip_length(&p, &length) && length == 60);
    p.len = 59;
    CHECK(!ring_link_netif_ip_length(&p, &length));
    p.len = 39; p.buffer[5] = 0;
    CHECK(!ring_link_netif_ip_length(&p, &length));
    p.len = 40;
    CHECK(ring_link_netif_ip_length(&p, &length) && length == 40);
    p.buffer[4] = (char)0xff; p.buffer[5] = (char)0xff;
    CHECK(!ring_link_netif_ip_length(&p, &length));
    return 0;
}

int test_transmit_releases_frame_on_success_and_failure(void) {
    reset();
    unsigned char bytes[RING_LINK_NETIF_MTU];
    memset(bytes, 0xa5, sizeof(bytes));
    CHECK(ring_link_netif_transmit(bytes, sizeof(bytes), 17) == ESP_OK);
    CHECK(allocations == 1 && frees == 1 && transmissions == 1);
    CHECK(sent.id == 17 && sent.ttl == 4 && sent.len == sizeof(bytes));
    CHECK(sent.src_id == 3 && sent.dst_id == CONFIG_ID_ANY);
    CHECK(sent.buffer_type == RING_LINK_PAYLOAD_TYPE_ESP_NETIF);
    CHECK(!memcmp(sent.buffer, bytes, sizeof(bytes)));
    transmit_result = ESP_FAIL;
    CHECK(ring_link_netif_transmit(bytes, 20, 18) == ESP_FAIL);
    CHECK(allocations == 2 && frees == 2 && transmissions == 2);
    CHECK(sent.len == 20 && sent.buffer[20] == 0 && sent.buffer[sizeof(bytes) - 1] == 0);
    return 0;
}

int test_transmit_allocation_failure_and_invalid_sizes(void) {
    reset();
    char byte = 0;
    allocation_fails = true;
    CHECK(ring_link_netif_transmit(&byte, 1, 0) == ESP_ERR_NO_MEM);
    CHECK(ring_link_netif_transmit(NULL, 1, 0) == ESP_ERR_INVALID_ARG);
    CHECK(ring_link_netif_transmit(&byte, 0, 0) == ESP_ERR_INVALID_ARG);
    CHECK(ring_link_netif_transmit(&byte, RING_LINK_NETIF_MTU + 1, 0) == ESP_ERR_INVALID_SIZE);
    CHECK(!allocations && !frees && !transmissions);
    return 0;
}

#define RUN(test) do { int line = test(); if (line) { \
    printf("%s: FAILED payload_test.c:%d\n", #test, line); return 1; } \
    printf("%s: OK\n", #test); } while (0)
int main(void) {
    RUN(test_ip_bounds_reject_invalid_header_and_oversized_frame);
    RUN(test_ipv6_uses_payload_length_and_fixed_header);
    RUN(test_transmit_releases_frame_on_success_and_failure);
    RUN(test_transmit_allocation_failure_and_invalid_sizes);
    return 0;
}
