#include "neighbor_manager/blocks.h"
#include <string.h>

static unsigned prefix(nm_network_t network) {
    unsigned bits = 0;
    for (uint32_t mask = network.mask; mask; mask <<= 1) ++bits;
    return bits;
}

bool nm_network_valid(nm_network_t network) {
    uint32_t hosts = ~network.mask;
    return network.addr && network.mask && !(hosts & (hosts + 1U)) &&
           !(network.addr & hosts) && prefix(network) <= 27;
}

static bool equal(nm_network_t a, nm_network_t b) {
    return a.addr == b.addr && a.mask == b.mask;
}

static bool contains(nm_network_t outer, nm_network_t inner) {
    return prefix(outer) <= prefix(inner) && (inner.addr & outer.mask) == outer.addr;
}

static bool overlap(nm_network_t a, nm_network_t b) {
    return contains(a, b) || contains(b, a);
}

void nm_blocks_init(nm_blocks_t *blocks, const char *provider_uuid) {
    memset(blocks, 0, sizeof(*blocks));
    blocks->version = NM_BLOCKS_VERSION;
    if (nm_uuid_valid(provider_uuid))
        memcpy(blocks->provider_uuid, provider_uuid, NM_UUID_LEN + 1);
}

bool nm_blocks_valid(const nm_blocks_t *blocks, const char *provider_uuid) {
    if (!blocks || blocks->version != NM_BLOCKS_VERSION || !nm_uuid_valid(provider_uuid) ||
        memcmp(blocks->provider_uuid, provider_uuid, NM_UUID_LEN + 1)) return false;
    for (unsigned i = 0; i < NM_BLOCK_LEASES; ++i) {
        const nm_block_lease_t *lease = &blocks->leases[i];
        if (!lease->network.addr && !lease->network.mask && !lease->uuid[0]) continue;
        if (!nm_network_valid(lease->network) || !nm_uuid_valid(lease->uuid) ||
            !memcmp(lease->uuid, provider_uuid, NM_UUID_LEN + 1)) return false;
        for (unsigned j = 0; j < i; ++j)
            if (blocks->leases[j].network.addr &&
                overlap(lease->network, blocks->leases[j].network)) return false;
    }
    return true;
}

static bool remember(nm_blocks_t *blocks, const char *uuid, nm_network_t network) {
    for (unsigned i = 0; i < NM_BLOCK_LEASES; ++i) {
        nm_block_lease_t *lease = &blocks->leases[i];
        if (lease->network.addr) continue;
        memset(lease, 0, sizeof(*lease));
        lease->network = network;
        memcpy(lease->uuid, uuid, NM_UUID_LEN + 1);
        return true;
    }
    return false;
}

bool nm_blocks_offer(nm_blocks_t *blocks, const char *peer_uuid,
                     nm_network_t parent, nm_network_t peer, unsigned preferred_sector,
                     nm_network_t *offer) {
    if (offer) *offer = (nm_network_t){0};
    if (!blocks || !offer || !nm_blocks_valid(blocks, blocks->provider_uuid) ||
        !nm_uuid_valid(peer_uuid) || !memcmp(peer_uuid, blocks->provider_uuid, NM_UUID_LEN + 1) ||
        !nm_network_valid(parent) || preferred_sector < 1 || preferred_sector > 4) return false;
    if (peer.addr || peer.mask) {
        if (!nm_network_valid(peer) || equal(peer, parent)) return false;
        if (contains(parent, peer)) {
            if (prefix(peer) < prefix(parent) + 3) return false;
            unsigned sector = (peer.addr >> (32 - prefix(parent) - 3)) & 7U;
            if (sector == 0 || sector == 5) return false;
        }
        for (unsigned i = 0; i < NM_BLOCK_LEASES; ++i) {
            const nm_block_lease_t *lease = &blocks->leases[i];
            if (!equal(lease->network, peer)) continue;
            if (memcmp(lease->uuid, peer_uuid, NM_UUID_LEN + 1)) return false;
            *offer = peer;
            return true;
        }
        /* Import a direct child's existing allocation (e.g. after migration).
         * A descendant with a smaller block may reconnect through an ancestor;
         * it does not acquire ownership of the ancestor's whole sector. */
        if (contains(parent, peer) && prefix(peer) == prefix(parent) + 3) {
            unsigned sector = (peer.addr >> (32 - prefix(peer))) & 7U;
            if (sector == 0 || sector == 5) return false;
            for (unsigned i = 0; i < NM_BLOCK_LEASES; ++i)
                if (blocks->leases[i].network.addr && overlap(peer, blocks->leases[i].network)) return false;
            if (!remember(blocks, peer_uuid, peer)) return false;
        }
        *offer = peer;
        return true;
    }
    /* Identity, not the current radio, owns an existing reservation. */
    for (unsigned i = 0; i < NM_BLOCK_LEASES; ++i) {
        if (!memcmp(blocks->leases[i].uuid, peer_uuid, NM_UUID_LEN + 1)) {
            nm_network_t home = {parent.addr | (5U << (32 - prefix(parent) - 3)),
                                 UINT32_MAX << (32 - prefix(parent) - 3)};
            if (contains(blocks->leases[i].network, parent) ||
                overlap(blocks->leases[i].network, home)) return false;
            *offer = blocks->leases[i].network;
            return true;
        }
    }
    unsigned child_prefix = prefix(parent) + 3;
    if (child_prefix > 27) return false;
    const unsigned sectors[] = {preferred_sector, 1, 2, 3, 4, 6, 7};
    for (unsigned i = 0; i < sizeof(sectors) / sizeof(sectors[0]); ++i) {
        nm_network_t candidate = {parent.addr | (sectors[i] << (32 - child_prefix)),
                                  UINT32_MAX << (32 - child_prefix)};
        bool free = true;
        for (unsigned j = 0; j < NM_BLOCK_LEASES; ++j)
            if (blocks->leases[j].network.addr && overlap(candidate, blocks->leases[j].network)) free = false;
        if (free) {
            if (!remember(blocks, peer_uuid, candidate)) return false;
            *offer = candidate;
            return true;
        }
    }
    return false;
}
