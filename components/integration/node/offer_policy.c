#include "offer_policy.h"
#include <stddef.h>

static unsigned prefix_size(uint32_t mask) {
    unsigned bits = 0;
    while (mask & 0x80000000U) { ++bits; mask <<= 1; }
    return mask == 0 ? bits : 33;
}

const char *node_offer_rejection(uint32_t device_subnet, uint32_t device_mask,
                        bool provisioned, bool original_uplink,
                        uint32_t offered_subnet, uint32_t offered_mask) {
    unsigned offered_prefix = prefix_size(offered_mask);
    /* A provisioned child must still have space for its five /n+3 blocks
     * and a /30 physical link. Do not overflow the allocator's shifts. */
    if (!offered_subnet) return "zero_offer";
    if (offered_prefix == 0 || offered_prefix > 26) return "offer_prefix_or_capacity";
    if ((offered_subnet & offered_mask) != offered_subnet) return "noncanonical_offer";
    if (!provisioned) return NULL;
    unsigned device_prefix = prefix_size(device_mask);
    if (device_prefix < 4 || device_prefix > 29 ||
        (device_subnet & device_mask) != device_subnet) return "invalid_local_block";
    unsigned node_prefix = device_prefix - 3;
    uint32_t node_mask = UINT32_MAX << (32 - node_prefix);
    uint32_t node_subnet = device_subnet & node_mask;
    /* Restoring the original uplink is not a new redundant-link search.
     * Its exact original allocation remains valid, without renumbering. */
    if (original_uplink && offered_subnet == node_subnet && offered_mask == node_mask)
        return NULL;
    if (offered_prefix <= node_prefix) return "prefix_not_greater";
    if (offered_subnet <= node_subnet) return "base_not_greater";
    return NULL;
}

bool node_offer_allowed(uint32_t device_subnet, uint32_t device_mask,
                        bool provisioned, bool original_uplink,
                        uint32_t offered_subnet, uint32_t offered_mask) {
    return node_offer_rejection(device_subnet, device_mask, provisioned,
                                original_uplink, offered_subnet, offered_mask) == NULL;
}
