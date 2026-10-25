#include "forwarder.h"

#include <string.h>

#include "os/os.h"
#include "routing/impl.h"
#include "../utils.h"

bool rt_fwd_default_missing(routing_t *self) {
    bool missing = false;
    WITH_LOCK(&self->node_state.m_lock, {
        missing = self->node_state.routing_table.default_gateway == 0;
    });
    return missing;
}

void rt_fwd_request_own_gateway(routing_t *self) {
    rt_forwarder_state_t *state = GET_STATE(self);
    if (!node_network_is_valid(&state->node_network, self->orientation)) return;
    /* Reuse the path-checked request/retry protocol instead of accepting a
     * longer bare DTR advertisement that could still depend on this node. */
    rt_sibl_send_new_gateway_request_t request = {0};
    self->role.impl.on_sibl_send_new_gateway_request(self, &request);
}

static void on_tick(routing_t *self, uint32_t dt_ms) {
    rt_forwarder_state_t *state = GET_STATE(self);
    if (state->global_state == GLOBAL_STATE_WITH_NETWORK && state->dtr > 1 &&
        state->local_state == LOCAL_STATE_CONNECTED && rt_fwd_default_missing(self)) {
        /* Shared-table loss is visible even when the sibling control packet
         * was lost. Start once; later ticks use the normal retry deadline. */
        rt_fwd_request_own_gateway(self);
        return;
    }
    if (state->provision_retry_timeout && state->is_local_root &&
        state->global_state == GLOBAL_STATE_WITH_NETWORK &&
        state->local_state == LOCAL_STATE_CONNECTED) {
        if (dt_ms < state->provision_retry_timeout) {
            state->provision_retry_timeout -= dt_ms;
        } else {
            /* At most one attempt per tick, even after a long scheduling gap.
             * Use the current network/DTR, never a stale queued provision. */
            rt_fwd_publish_provision(self);
        }
    }
    if (state->global_state != GLOBAL_STATE_ON_GW_REQUEST ||
        state->local_state != LOCAL_STATE_CONNECTED ||
        state->gateway_request_timeout == 0)
        return;

    if (dt_ms < state->gateway_request_timeout) {
        state->gateway_request_timeout -= dt_ms;
        return;
    }

    rt_peer_event_t retry = { .event_id = PEER_NEW_GATEWAY_REQUEST };
    memcpy(retry.payload.new_gateway_request.hag_networks,
           state->gateway_request_path, sizeof(state->gateway_request_path));
    state->gateway_request_timeout = GATEWAY_REQUEST_RETRY_MS;
    wl_send_peer_message(self->deps.wl, &retry, sizeof(retry));
}

bool create_forwarder_core(routing_t *self) {
    rt_forwarder_state_t *state = GET_STATE(self);

    state->global_state = GLOBAL_STATE_WITHOUT_NETWORK;
    state->local_state = LOCAL_STATE_NOT_CONNECTED;
    state->node_network = NETWORK(0, 0);
    state->device_network = NETWORK(0, 0);
    state->is_local_root = false;
    state->dtr = 0;
    state->gateway_request_timeout = 0;
    state->provision_retry_timeout = 0;

    rt_fwd_sibl_set_required_callbacks(&self->role.impl);
    rt_fwd_peer_set_required_callbacks(&self->role.impl);
    self->role.impl.on_tick = on_tick;

    return true;
}
