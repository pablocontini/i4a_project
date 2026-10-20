#include "routing/routing.h"

#include "os/os.h"

#include "impl_priv.h"
#include "routing/impl.h"
#include "utils.h"

#define TAG "home"
#define GET_STATE(self) (&((self)->role.state.home))

static void on_start(routing_t *self) {
    WITH_LOCK(&self->queue_lock, { self->pending_publish = true; });
}

static void on_tick(routing_t *self, uint32_t dt_ms) {
    (void)dt_ms;
    rt_home_state_t *state = GET_STATE(self);
    if (!state->is_provisioned) return;

    bool changed = false;
    WITH_LOCK(&self->node_state.m_lock, {
        rt_routing_table_t *table = &self->node_state.routing_table;
        bool present = false;
        for (size_t i = 0; i < table->count; ++i) {
            const rt_routing_entry_t *entry = &table->entries[i];
            if (entry->network == state->device_network.addr &&
                entry->mask == state->device_network.mask &&
                entry->output == self->orientation) {
                present = true;
                break;
            }
        }
        if (!present) {
            /* Reconcile the local invariant after an incomplete shared table.
             * Keep WAN/lateral entries and do not reconfigure or restart Wi-Fi. */
            routing_table_add(table, state->device_network.addr,
                              state->device_network.mask, self->orientation);
            changed = true;
        }
    });
    if (changed) {
        WITH_LOCK(&self->queue_lock, { self->pending_publish = true; });
    }
}

static void on_provision(routing_t *self, const rt_sibl_provision_t *provision) {
    rt_home_state_t *state = GET_STATE(self);

    if (state->is_provisioned) {
        log_warn(
            TAG, "[on_provision] Home device already provisioned -- skipping new provision (provider: %u)",
            provision->provider_id
        );
        return;
    }

    network_t my_subnet = get_node_subnet(&provision->network, self->orientation);
    if (my_subnet.mask == 0 || provision->dtr == 0)
        return;
    log_warn(TAG, "[on_provision] Got subnet %08X/%d", my_subnet.addr, mask_size(my_subnet.mask));

    add_global_route(self, &my_subnet, self->orientation);
    state->device_network = my_subnet;
    log_info(TAG, "[on_provision] Added entry to node routing table");

    wl_enable_ap_mode(self->deps.wl, my_subnet.addr, my_subnet.mask);
    log_info(TAG, "[on_provision] Enabled AP mode");

    state->is_provisioned = true;
}

bool create_home_core(routing_t *self) {
    rt_home_state_t *state = GET_STATE(self);
    state->is_provisioned = false;
    state->device_network = NETWORK(0, 0);

    self->role.impl = (rt_role_impl_t){
        .on_start = on_start,
        .on_tick = on_tick,
        .on_sibl_provision = on_provision,
    };

    return true;
}
