#include "utils.h"

#include <stdint.h>

#include "routing/routing.h"

uint32_t mask_size(uint32_t n) {
    uint32_t c = 0;

    for (; n; ++c) {
        n <<= 1;
    }

    return c;
}

bool node_network_is_valid(const network_t *node_network, orientation_t orientation) {
    if (!node_network || !node_network->addr || !node_network->mask ||
        orientation < ORIENTATION_NORTH || orientation > ORIENTATION_CENTER)
        return false;

    uint32_t host_bits = ~node_network->mask;
    if ((host_bits & (host_bits + UINT32_C(1))) != 0 ||
        (node_network->addr & host_bits) != 0)
        return false;

    return mask_size(node_network->mask) <= 27;
}

network_t get_node_subnet(const network_t *node_network, orientation_t orientation) {
    if (!node_network_is_valid(node_network, orientation))
        return NETWORK(0, 0);

    uint32_t prefix_len = mask_size(node_network->mask) + 3U;
    uint32_t host_shift = 32U - prefix_len;
    uint32_t new_mask = UINT32_MAX << host_shift;
    uint32_t new_network = node_network->addr | ((uint32_t)orientation << host_shift);
    return (network_t){
        .addr = new_network,
        .mask = new_mask,
    };
}

void add_global_route(routing_t *self, const network_t *route, orientation_t output) {
    WITH_LOCK(&self->node_state.m_lock, {
        routing_table_add(&self->node_state.routing_table, route->addr, route->mask, output);
    });
    WITH_LOCK(&self->queue_lock, { self->pending_publish = true; });
}

void remove_routes_by_output(routing_t *self, orientation_t output) {
    WITH_LOCK(&self->node_state.m_lock, {
        routing_table_remove_by_output(&self->node_state.routing_table, output);
        if (self->node_state.routing_table.default_gateway == output)
            self->node_state.routing_table.default_gateway = 0;
    });
    WITH_LOCK(&self->queue_lock, { self->pending_publish = true; });
}

network_t *find_free_spot(network_t networks[], size_t length) {
    for (size_t i = 0; i < length; i++) {
        if (networks[i].addr == 0)
            return &networks[i];
    }

    return NULL;
}
