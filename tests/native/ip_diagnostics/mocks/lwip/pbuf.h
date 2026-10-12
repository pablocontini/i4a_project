#ifndef IP_DIAGNOSTICS_TEST_PBUF_H
#define IP_DIAGNOSTICS_TEST_PBUF_H
#include <stdint.h>
struct pbuf {
    uint16_t len, tot_len;
    const void *payload;
    struct pbuf *next;
};
uint16_t pbuf_copy_partial(const struct pbuf *p, void *destination, uint16_t len, uint16_t offset);
#endif
