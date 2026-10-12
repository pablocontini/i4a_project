#ifndef I4A_IP_DIAGNOSTICS_H
#define I4A_IP_DIAGNOSTICS_H

#include <stddef.h>
#include <stdint.h>

struct pbuf;
typedef enum {
    IP_DIAG_SPI_RX,
    IP_DIAG_SPI_RX_BAD_LENGTH,
    IP_DIAG_SPI_RX_ALLOC_FAIL,
    IP_DIAG_SPI_INPUT,
    IP_DIAG_SPI_INPUT_ACCEPTED,
    IP_DIAG_SPI_INPUT_REJECTED,
    IP_DIAG_SPI_TX_ATTEMPT,
    IP_DIAG_SPI_TX_OK,
    IP_DIAG_SPI_TX_FAIL,
    IP_DIAG_SPI_GATE_DROP,
    IP_DIAG_SPI_WIRE_TX_OK,
    IP_DIAG_SPI_WIRE_TX_FAIL,
    IP_DIAG_STAGE_COUNT
} ip_diag_stage_t;

typedef enum {
    IP_DIAG_ROUTE_SPI,
    IP_DIAG_ROUTE_WIFI,
    IP_DIAG_ROUTE_SINK,
    IP_DIAG_ROUTE_FALLBACK,
    IP_DIAG_ROUTE_OTHER,
    IP_DIAG_ROUTE_COUNT
} ip_diag_route_t;

/* Read-only diagnostics. data is a contiguous IPv4 prefix, captured_len is
 * its available byte count (68 bytes suffice even with IPv4 options).
 * Only complete, unfragmented ICMP echo headers are counted. No UDP or ICMP
 * error messages are counted. The prefix may describe a larger packet.
 * Counts are cumulative since boot; emission is at most once per 5 seconds
 * per stage and uses the bounded nonblocking os_diag queue. */
void ip_diag_packet(const void *data, size_t captured_len,
                    ip_diag_stage_t stage, int error);
/* Copy a bounded prefix safely from a chained pbuf; never takes ownership. */
void ip_diag_pbuf(const struct pbuf *packet, ip_diag_stage_t stage, int error);
/* Addresses use host-order routing notation, e.g. 10.0.0.1 = 0x0a000001.
 * Route hooks have no packet/protocol: these counts are route lookups,
 * including UDP lookups, never evidence of ICMP handoff. */
void ip_diag_route(uint32_t src, uint32_t dst, ip_diag_route_t decision);

#endif
