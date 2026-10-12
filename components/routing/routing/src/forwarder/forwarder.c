#include "forwarder.h"

#include "os/os.h"
#include "routing/impl.h"
#include "../utils.h"
#include <string.h>

bool rt_fwd_prepare_gateway_request(routing_t *self, const network_t path[16]) {
    rt_forwarder_state_t *state = GET_STATE(self);
    memcpy(state->gateway_request_path, path, sizeof(state->gateway_request_path));
    network_t *slot = find_free_spot(state->gateway_request_path, 16);
    if (!slot) {
        log_error(TAG, "Gateway request path is full");
        return false;
    }
    *slot = state->node_network;
    state->gateway_retry_timeout = GATEWAY_RETRY_TIMEOUT_MS;
    return true;
}

void rt_fwd_send_gateway_request(routing_t *self) {
    rt_forwarder_state_t *state = GET_STATE(self);
    if (state->local_state != LOCAL_STATE_CONNECTED || !state->node_network.mask)
        return;
    rt_peer_event_t event = { .event_id = PEER_NEW_GATEWAY_REQUEST };
    memcpy(event.payload.new_gateway_request.hag_networks, state->gateway_request_path,
           sizeof(state->gateway_request_path));
    bool sent=wl_send_peer_message(self->deps.wl,&event,sizeof(event));
    os_diag("gateway","event=request_send orientation=%u connected=%u dtr=%lu sent=%u",self->orientation,state->local_state,(unsigned long)state->dtr,sent);
}

static bool tick_needed(routing_t *self) {
    rt_forwarder_state_t *state = GET_STATE(self);
    return state->global_state == GLOBAL_STATE_ON_GW_REQUEST && state->gateway_retry_timeout != 0;
}

static void on_tick(routing_t *self, uint32_t dt_ms) {
    rt_forwarder_state_t *state = GET_STATE(self);
    if (state->global_state != GLOBAL_STATE_ON_GW_REQUEST || !state->gateway_retry_timeout)
        return;
    if (dt_ms < state->gateway_retry_timeout) {
        state->gateway_retry_timeout -= dt_ms;
        return;
    }
    state->gateway_retry_timeout = GATEWAY_RETRY_TIMEOUT_MS;
    rt_sibl_event_t retry = {.event_id = SIBL_SEND_NEW_GATEWAY_REQUEST};
    memcpy(retry.payload.send_new_gateway_request.hag_networks,
           state->gateway_request_path, sizeof(state->gateway_request_path));
    // Siblings append this node themselves. Recover an initial SPI broadcast
    // lost during OTA even when this interface has no remaining wireless peer.
    for (size_t i = 0; i < 16; ++i) {
        network_t *hop = &retry.payload.send_new_gateway_request.hag_networks[i];
        if (hop->addr == state->node_network.addr && hop->mask == state->node_network.mask) {
            memset(hop, 0, sizeof(*hop));
            break;
        }
    }
    rs_broadcast(self->deps.rs, RS_ROUTING, &retry, sizeof(retry));
    rt_fwd_send_gateway_request(self);
}

bool create_forwarder_core(routing_t *self) {
    rt_forwarder_state_t *state = GET_STATE(self);

    state->global_state = GLOBAL_STATE_WITHOUT_NETWORK;
    state->local_state = LOCAL_STATE_NOT_CONNECTED;
    state->node_network = NETWORK(0, 0);
    state->device_network = NETWORK(0, 0);
    state->is_local_root = false;
    state->dtr = 0;
    state->gateway_retry_timeout = 0;
    memset(state->gateway_request_path, 0, sizeof(state->gateway_request_path));

    self->role.impl.on_tick = on_tick;
    self->role.impl.tick_needed = tick_needed;
    rt_fwd_sibl_set_required_callbacks(&self->role.impl);
    rt_fwd_peer_set_required_callbacks(&self->role.impl);

    return true;
}
