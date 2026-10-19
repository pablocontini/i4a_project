#include "forwarder.h"

#include <string.h>

#include "os/os.h"
#include "routing/impl.h"

static void on_tick(routing_t *self, uint32_t dt_ms) {
    rt_forwarder_state_t *state = GET_STATE(self);
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

    rt_fwd_sibl_set_required_callbacks(&self->role.impl);
    rt_fwd_peer_set_required_callbacks(&self->role.impl);
    self->role.impl.on_tick = on_tick;

    return true;
}
