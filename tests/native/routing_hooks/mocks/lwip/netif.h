#ifndef NATIVE_ROUTING_HOOKS_NETIF_H
#define NATIVE_ROUTING_HOOKS_NETIF_H
#include <stdint.h>
#define ERR_OK 0
#define ERR_RTE -4
#define NETIF_FLAG_UP 1
#define NETIF_FLAG_LINK_UP 4
typedef int err_t;
typedef struct { uint32_t addr; } ip4_addr_t;
struct pbuf { unsigned len; };
struct netif {
    char name[2];
    unsigned flags, mtu;
    err_t (*output)(struct netif *, struct pbuf *, const ip4_addr_t *);
};
#define netif_is_up(netif) (((netif)->flags & NETIF_FLAG_UP) != 0)
#endif
