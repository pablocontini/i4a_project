#include "ip_diagnostics.h"
#include "lwip/pbuf.h"
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) return __LINE__; } while (0)
unsigned diagnostic_lock_depth;
static uint64_t clock_ms;
static unsigned emitted, copied_prefix_bytes;
static char last_log[176];

uint64_t os_monotonic_ms(void) { assert(!diagnostic_lock_depth); return clock_ms; }
unsigned config_get_id(void) { assert(!diagnostic_lock_depth); return 4; }
void os_diag(const char *tag, const char *fmt, ...) {
    assert(!diagnostic_lock_depth);
    assert(!strcmp(tag, "ip"));
    va_list args; va_start(args, fmt);
    int length = vsnprintf(last_log, sizeof(last_log), fmt, args);
    va_end(args);
    assert(length >= 0 && length < sizeof(last_log));
    ++emitted;
}
uint16_t pbuf_copy_partial(const struct pbuf *p, void *destination,
                           uint16_t len, uint16_t offset) {
    uint16_t copied = 0;
    assert(len <= 68);
    for (; p && copied < len; p = p->next) {
        if (offset >= p->len) { offset -= p->len; continue; }
        uint16_t part = p->len - offset;
        if (part > len - copied) part = len - copied;
        memcpy((uint8_t *)destination + copied, (const uint8_t *)p->payload + offset, part);
        copied += part; offset = 0;
    }
    copied_prefix_bytes += copied;
    return copied;
}
static void echo(uint8_t *bytes, unsigned header_len, unsigned type) {
    memset(bytes, 0, header_len + 8);
    bytes[0] = 0x40 | (header_len / 4);
    bytes[3] = header_len + 8;
    bytes[9] = 1;
    bytes[12] = 10; bytes[13] = 148; bytes[15] = 2;
    bytes[16] = 10; bytes[17] = bytes[18] = 255; bytes[19] = 254;
    bytes[header_len] = type;
    bytes[header_len + 4] = 0x12; bytes[header_len + 5] = 0x34;
    bytes[header_len + 7] = 99;
}

static int test_non_echo_and_invalid_headers_emit_nothing(void) {
    uint8_t packet[68];
    unsigned before = emitted;
    echo(packet, 20, 8);
    ip_diag_packet(NULL, 28, IP_DIAG_SPI_RX, 0);
    ip_diag_packet(packet, 0, IP_DIAG_SPI_RX, 0);
    ip_diag_packet(packet, 27, IP_DIAG_SPI_RX, 0);
    packet[0] = 0x44; ip_diag_packet(packet, 28, IP_DIAG_SPI_RX, 0);
    packet[0] = 0x65; ip_diag_packet(packet, 28, IP_DIAG_SPI_RX, 0);
    packet[0] = 0x45; packet[9] = 17; ip_diag_packet(packet, 28, IP_DIAG_SPI_RX, 0);
    packet[9] = 1; packet[20] = 3; ip_diag_packet(packet, 28, IP_DIAG_SPI_RX, 0);
    packet[20] = 8; packet[21] = 1; ip_diag_packet(packet, 28, IP_DIAG_SPI_RX, 0);
    packet[21] = 0; packet[3] = 27; ip_diag_packet(packet, 28, IP_DIAG_SPI_RX, 0);
    packet[3] = 28; packet[6] = 0x20; ip_diag_packet(packet, 28, IP_DIAG_SPI_RX, 0);
    packet[6] = 0; packet[7] = 1; ip_diag_packet(packet, 28, IP_DIAG_SPI_RX, 0);
    packet[7] = 0; ip_diag_packet(packet, 28, IP_DIAG_STAGE_COUNT, 0);
    CHECK(emitted == before);
    return 0;
}

static int test_options_unaligned_header_and_bounded_prefix(void) {
    uint8_t buffer[69], *packet = buffer + 1;
    echo(packet, 60, 8);
    unsigned before = emitted;
    for (unsigned len = 0; len < 68; ++len)
        ip_diag_packet(packet, len, IP_DIAG_SPI_RX, 0);
    CHECK(emitted == before);
    packet[2] = 5; packet[3] = 0; /* Only prefix captured, larger valid IP total. */
    ip_diag_packet(packet, 68, IP_DIAG_SPI_RX, -1);
    CHECK(emitted == before + 1);
    CHECK(strstr(last_log, "s=10.148.0.2 d=10.255.255.254 id=4660 seq=99 err=-1"));
    CHECK(strstr(last_log, "req=1 reply=0"));
    return 0;
}

static int test_rate_limit_counts_requests_and_replies_cumulatively(void) {
    uint8_t packet[28]; echo(packet, 20, 8);
    unsigned before = emitted;
    clock_ms = 0;
    ip_diag_packet(packet, 28, IP_DIAG_SPI_TX_ATTEMPT, 0);
    CHECK(emitted == before + 1);
    clock_ms = 4999;
    for (unsigned i = 0; i < 99; ++i) ip_diag_packet(packet, 28, IP_DIAG_SPI_TX_ATTEMPT, 0);
    packet[20] = 0;
    for (unsigned i = 0; i < 50; ++i) ip_diag_packet(packet, 28, IP_DIAG_SPI_TX_ATTEMPT, 0);
    CHECK(emitted == before + 1);
    clock_ms = 5000; packet[20] = 8;
    ip_diag_packet(packet, 28, IP_DIAG_SPI_TX_ATTEMPT, -12);
    CHECK(emitted == before + 2);
    CHECK(strstr(last_log, "req=101 reply=50"));
    CHECK(strstr(last_log, "err=-12"));
    /* Another stage has its own first emission, never shares suppression. */
    ip_diag_packet(packet, 28, IP_DIAG_SPI_TX_FAIL, -12);
    CHECK(emitted == before + 3);
    return 0;
}

static int test_chained_pbuf_keeps_ownership_and_copies_only_prefix(void) {
    uint8_t packet[100]; echo(packet, 60, 0);
    struct pbuf tail = {.len = 98, .tot_len = 98, .payload = packet + 2};
    struct pbuf first = {.len = 2, .tot_len = 100, .payload = packet, .next = &tail};
    unsigned before = emitted, copies = copied_prefix_bytes;
    ip_diag_pbuf(&first, IP_DIAG_SPI_INPUT_ACCEPTED, 0);
    CHECK(emitted == before + 1 && copied_prefix_bytes == copies + 68);
    CHECK(strstr(last_log, "req=0 reply=1"));
    CHECK(first.next == &tail && first.len == 2 && packet[0] == 0x4f);
    ip_diag_pbuf(NULL, IP_DIAG_SPI_INPUT_ACCEPTED, 0);
    CHECK(emitted == before + 1);
    return 0;
}

static int test_route_lookup_is_distinct_and_addresses_are_host_order(void) {
    unsigned before = emitted;
    ip_diag_route(0x0a940002, 0x08080808, IP_DIAG_ROUTE_SPI);
    ip_diag_route(0x0a940002, 0x0afffffe, IP_DIAG_ROUTE_COUNT);
    CHECK(emitted == before);
    clock_ms = 10000;
    ip_diag_route(0x0a940002, 0x0afffffe, IP_DIAG_ROUTE_SPI);
    CHECK(emitted == before + 1);
    CHECK(strstr(last_log, "event=route_lookup board_id=4 via=spi count=1"));
    CHECK(strstr(last_log, "s=10.148.0.2 d=10.255.255.254"));
    CHECK(!strstr(last_log, "icmp"));
    ip_diag_route(0x0a940002, 0x01010101, IP_DIAG_ROUTE_SPI);
    CHECK(emitted == before + 1);
    clock_ms = 15000;
    ip_diag_route(0x0a940002, 0x01010101, IP_DIAG_ROUTE_SPI);
    CHECK(emitted == before + 2 && strstr(last_log, "count=3"));
    ip_diag_route(0x0a940002, 0x01010101, IP_DIAG_ROUTE_SINK);
    CHECK(emitted == before + 3 && strstr(last_log, "via=sink"));
    return 0;
}

#define RUN(test) do { int line = test(); if (line) { \
    printf("%s: FAILED diagnostics_test.c:%d\n", #test, line); return 1; } \
    printf("%s: OK\n", #test); } while (0)
int main(void) {
    RUN(test_non_echo_and_invalid_headers_emit_nothing);
    RUN(test_options_unaligned_header_and_bounded_prefix);
    RUN(test_rate_limit_counts_requests_and_replies_cumulatively);
    RUN(test_chained_pbuf_keeps_ownership_and_copies_only_prefix);
    RUN(test_route_lookup_is_distinct_and_addresses_are_host_order);
    return 0;
}
