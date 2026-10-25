#ifndef I4A_NEIGHBOR_BLOCKS_H
#define I4A_NEIGHBOR_BLOCKS_H

#include "neighbor_manager/registry.h"

#define NM_BLOCK_LEASES 16
#define NM_BLOCKS_VERSION 1

typedef struct { uint32_t addr, mask; } nm_network_t;
typedef struct {
    nm_network_t network;
    char uuid[NM_UUID_LEN + 1];
} nm_block_lease_t;
typedef struct {
    uint32_t version;
    char provider_uuid[NM_UUID_LEN + 1];
    nm_block_lease_t leases[NM_BLOCK_LEASES];
} nm_blocks_t;

void nm_blocks_init(nm_blocks_t *blocks, const char *provider_uuid);
bool nm_network_valid(nm_network_t network);
bool nm_blocks_valid(const nm_blocks_t *blocks, const char *provider_uuid);
/* Plan on a copy; the caller must persist it before advertising the offer.
 * Reservations have no timeout: a disconnected child may still use its block. */
bool nm_blocks_offer(nm_blocks_t *blocks, const char *peer_uuid,
                     nm_network_t parent, nm_network_t peer, unsigned preferred_sector,
                     nm_network_t *offer);

bool nm_block_store_load(const char *provider_uuid, nm_blocks_t *blocks);
bool nm_block_store_save(const nm_blocks_t *blocks);

#endif
