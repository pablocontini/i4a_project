#ifndef ROUTING_TEST_NETIF_H
#define ROUTING_TEST_NETIF_H
#include <stdint.h>
typedef int err_t;
#define ERR_OK 0
#define ERR_RTE -4
#define NETIF_FLAG_UP 1
#define NETIF_FLAG_LINK_UP 2
struct pbuf { uint16_t len; };
typedef struct { uint32_t addr; } ip4_addr_t;
struct netif {
    char name[2];
    uint16_t mtu;
    uint8_t flags;
    err_t (*output)(struct netif *, struct pbuf *, const ip4_addr_t *);
};
#define netif_is_up(netif) ((netif)->flags & NETIF_FLAG_UP)
#endif
