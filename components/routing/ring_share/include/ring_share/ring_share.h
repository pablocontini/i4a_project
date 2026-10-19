#ifndef _I4A_RING_SHARE_
#define _I4A_RING_SHARE_

#ifdef __cplusplus
#define _Static_assert static_assert
extern "C" {
#endif

#include <stdint.h>

#include "os/os.h"
#include "siblings/siblings.h"

#define RS_MAX_BROADCAST_LEN 512
#define RS_MAX_COMPONENTS 10

typedef enum component_id {
    RS_SYNC = 0,
    RS_ROUTING = 1,
    RS_SHARED_STATE = 2,
    RS_CHANNEL_MANAGER = 3,
    RS_RESET_MANAGER = 4,
    RS_INFO_MANAGER = 5,
    RS_PRIORITY_MANAGER = 6,
    RS_NODE_OTA = 7,
    RS_NEIGHBOR_MANAGER = 8,

    /* Keep this variant last */
    RS_LAST_COMPONENT_ID,
} component_id_t;

_Static_assert(RS_LAST_COMPONENT_ID <= RS_MAX_COMPONENTS, "Too many components, raise RS_MAX_COMPONENT");

typedef void (*ring_share_callback_fn_t)(void *ctx, const uint8_t *msg, uint16_t len);

typedef struct ring_callback {
    ring_share_callback_fn_t callback;
    void *context;
} ring_callback_t;

typedef struct ring_share {
    siblings_t *siblings;
    mutex_t broadcast_lock;
    mutex_t maintenance_lock;
    bool maintenance, boot_quiet;
    int64_t paused_since_us, paused_total_us;
    uint8_t buffer[RS_MAX_BROADCAST_LEN + 1];
    ring_callback_t components[RS_MAX_COMPONENTS];
} ring_share_t;

bool rs_init(ring_share_t *self, siblings_t *sb);

void rs_register_component(ring_share_t *self, component_id_t component, ring_callback_t callback);

bool rs_broadcast(ring_share_t *self, component_id_t component, const void *msg, uint16_t len);

/* Drain any active send, then admit OTA and reset/startup control until released.
 * Other sends fail promptly: a sibling callback must never wait for OTA. */
void rs_set_maintenance(ring_share_t *self, bool active);
bool rs_maintenance(ring_share_t *self);
/* Block neighbor admission/IP during startup; keep provisioning and reset alive. */
void rs_set_boot_quiet(ring_share_t *self, bool active);
bool rs_boot_quiet(ring_share_t *self);
/* Local lease clock: stopped during maintenance, monotonic on resume. */
uint64_t rs_lease_time_ms(ring_share_t *self);

void rs_shutdown(ring_share_t *self);

#ifdef __cplusplus
}
#endif


#endif  // _I4A_RING_SHARE_

