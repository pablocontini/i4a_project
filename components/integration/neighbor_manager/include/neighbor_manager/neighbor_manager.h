#ifndef I4A_NEIGHBOR_MANAGER_H
#define I4A_NEIGHBOR_MANAGER_H

#include "neighbor_manager/registry.h"
#include "neighbor_manager/blocks.h"
#include "ring_share/ring_share.h"

/* The center is the only writer; all radios retain snapshots for scan filtering. */
bool nm_init(ring_share_t *rs, uint8_t orientation, const char *node_uuid);
bool nm_reserve(const char *peer_uuid, const nm_link_id_t *link);
bool nm_commit(const nm_link_id_t *link);
bool nm_activate(const nm_link_id_t *link);
/* AP-only: the center durably reserves a logical child block before admission.
 * Call from the session worker, never a sibling/routing callback. */
bool nm_provide_block(const nm_link_id_t *link, nm_network_t parent,
                      nm_network_t peer_network, nm_network_t *offer);
bool nm_renew(const nm_link_id_t *link);
void nm_release(const nm_link_id_t *link);
bool nm_owns(const nm_link_id_t *link);
bool nm_is_blocked_uuid(const char *uuid);
/* Diagnostic, RAM-only guard. Existing active links and renewals survive.
 * New Root admissions are refused for <=900 seconds of real elapsed time. */
bool nm_guard_root(uint32_t seconds);
uint32_t nm_root_guard_remaining_ms(void);
/* Read-only local view for diagnostics; no SPI exchange or lease mutation. */
bool nm_copy_snapshot(nm_registry_t *registry);
void nm_tick(void);

#endif
