#ifndef NATIVE_PORTAL_RING_SHARE_RING_SHARE_H
#define NATIVE_PORTAL_RING_SHARE_RING_SHARE_H
#include <stdint.h>
#include <stdbool.h>
typedef struct { int unused; } ring_share_t;
typedef struct {
    void (*callback)(void *, const uint8_t *, uint16_t);
    void *context;
} ring_callback_t;
#define RS_MAX_BROADCAST_LEN 512
#define RS_RESET_MANAGER 4
void rs_register_component(ring_share_t *, int, ring_callback_t);
bool rs_broadcast(ring_share_t *, int, const void *, uint16_t);

#endif
