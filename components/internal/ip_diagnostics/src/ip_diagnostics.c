#include "ip_diagnostics.h"
#include "config.h"
#include "os/os.h"
#include "freertos/FreeRTOS.h"
#include "lwip/pbuf.h"
#include <stdbool.h>

#define REPORT_INTERVAL_MS 5000u
#define IP_PREFIX_SIZE 68u
#define OCTETS(a) (unsigned)((a) >> 24), (unsigned)(((a) >> 16) & 255), \
                  (unsigned)(((a) >> 8) & 255), (unsigned)((a) & 255)

typedef struct {
    uint32_t request, reply;
    uint64_t reported_ms;
    bool reported;
} packet_counts_t;
typedef struct {
    uint32_t count;
    uint64_t reported_ms;
    bool reported;
} route_counts_t;

static portMUX_TYPE counts_lock = portMUX_INITIALIZER_UNLOCKED;
static packet_counts_t packet_counts[IP_DIAG_STAGE_COUNT];
static route_counts_t route_counts[IP_DIAG_ROUTE_COUNT];
static const char *const stage_names[IP_DIAG_STAGE_COUNT] = {
    "spi_rx", "spi_rx_bad_length", "spi_rx_alloc_fail", "spi_input",
    "spi_input_ok", "spi_input_reject", "spi_tx_attempt", "spi_tx_ok",
    "spi_tx_fail", "spi_gate_drop", "spi_wire_tx_ok", "spi_wire_tx_fail"
};
static const char *const route_names[IP_DIAG_ROUTE_COUNT] = {
    "spi", "wifi", "sink", "fallback", "other"
};

static uint16_t read_u16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}
static uint32_t read_u32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

void ip_diag_packet(const void *data, size_t captured_len,
                    ip_diag_stage_t stage, int error) {
    if (!data || (unsigned)stage >= IP_DIAG_STAGE_COUNT || captured_len < 20)
        return;
    const uint8_t *bytes = data;
    size_t header_len = (size_t)(bytes[0] & 15) * 4;
    if ((bytes[0] >> 4) != 4 || header_len < 20 || header_len > 60 ||
        captured_len < header_len + 8 || bytes[9] != 1 ||
        read_u16(bytes + 2) < header_len + 8 ||
        (read_u16(bytes + 6) & 0x3fff) != 0)
        return;
    const uint8_t *icmp = bytes + header_len;
    if ((icmp[0] != 8 && icmp[0] != 0) || icmp[1] != 0) return;

    uint32_t src = read_u32(bytes + 12), dst = read_u32(bytes + 16);
    uint16_t id = read_u16(icmp + 4), sequence = read_u16(icmp + 6);
    uint64_t now = os_monotonic_ms();
    packet_counts_t snapshot;
    bool emit;
    /* Only increments and a fixed-size snapshot are inside the critical
     * section. Parsing, formatting, role lookup and queueing stay outside. */
    portENTER_CRITICAL(&counts_lock);
    packet_counts_t *counts = &packet_counts[stage];
    if (icmp[0] == 8) ++counts->request; else ++counts->reply;
    emit = !counts->reported || now - counts->reported_ms >= REPORT_INTERVAL_MS;
    if (emit) {
        counts->reported = true;
        counts->reported_ms = now;
    }
    snapshot = *counts;
    portEXIT_CRITICAL(&counts_lock);
    if (!emit) return;
    os_diag("ip", "event=icmp board_id=%u stage=%s req=%lu reply=%lu s=%u.%u.%u.%u d=%u.%u.%u.%u id=%u seq=%u err=%d",
            (unsigned)config_get_id(), stage_names[stage],
            (unsigned long)snapshot.request, (unsigned long)snapshot.reply,
            OCTETS(src), OCTETS(dst), (unsigned)id, (unsigned)sequence, error);
}

void ip_diag_pbuf(const struct pbuf *packet, ip_diag_stage_t stage, int error) {
    if (!packet) return;
    uint8_t prefix[IP_PREFIX_SIZE];
    uint16_t copy_len = packet->tot_len < sizeof(prefix) ?
                        packet->tot_len : (uint16_t)sizeof(prefix);
    uint16_t copied = pbuf_copy_partial(packet, prefix, copy_len, 0);
    ip_diag_packet(prefix, copied, stage, error);
}

void ip_diag_route(uint32_t src, uint32_t dst, ip_diag_route_t decision) {
    if ((unsigned)decision >= IP_DIAG_ROUTE_COUNT ||
        ((dst >> 24) != 10 && dst != 0x01010101u)) return;
    uint64_t now = os_monotonic_ms();
    uint32_t count;
    bool emit;
    portENTER_CRITICAL(&counts_lock);
    route_counts_t *counts = &route_counts[decision];
    count = ++counts->count;
    emit = !counts->reported || now - counts->reported_ms >= REPORT_INTERVAL_MS;
    if (emit) {
        counts->reported = true;
        counts->reported_ms = now;
    }
    portEXIT_CRITICAL(&counts_lock);
    if (!emit) return;
    os_diag("ip", "event=route_lookup board_id=%u via=%s count=%lu s=%u.%u.%u.%u d=%u.%u.%u.%u",
            (unsigned)config_get_id(), route_names[decision],
            (unsigned long)count, OCTETS(src), OCTETS(dst));
}
