#ifndef I4A_DISCOVERY_MANAGER_H
#define I4A_DISCOVERY_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include "ring_share/ring_share.h"

/* Deadlines use real esp_timer time, including OTA intervals. A radio must
 * stop scanning/retrying and return to AP before its local deadline. The
 * center retains 30 s of grace and waits one central lease after boot. */
#define DM_SEARCH_MS 60000
#define DM_CENTER_MS 90000

typedef struct {
    uint64_t deadline_ms;
    uint32_t session;
    uint32_t request;
    uint8_t owner;
} dm_token_t;

/* Orientation is 0..3 for peripheral radios, 4 for the single arbiter. */
bool dm_init(ring_share_t *rs, uint8_t orientation);
/* Bounded RPC; do not call from a sibling callback. There is no renewal.
 * Initial allocation (provisioned=false) waits until all local radios are
 * free, including pending neighbor admissions. Additional discovery only
 * requires the requesting radio to be free. */
bool dm_acquire(dm_token_t *token, bool provisioned);
/* Check before each scan, connection attempt and retry. False during OTA. */
bool dm_owns(const dm_token_t *token);
/* Call after leaving Station search mode. An old token cannot release a new
 * turn; a failed release leaves the center's finite lease to expire. */
void dm_release(const dm_token_t *token);

#endif
