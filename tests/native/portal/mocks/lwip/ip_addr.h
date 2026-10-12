#ifndef NATIVE_PORTAL_LWIP_IP_ADDR_H
#define NATIVE_PORTAL_LWIP_IP_ADDR_H
#include <stdint.h>
static inline uint32_t htonl(uint32_t x) {
    return ((x & 0xffu) << 24) | ((x & 0xff00u) << 8) |
           ((x >> 8) & 0xff00u) | ((x >> 24) & 0xffu);
}
#define ntohl(x) htonl(x)

#endif
