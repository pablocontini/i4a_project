#include "forwarder.h"

#include <string.h>

#include "../utils.h"
#include "os/os.h"

static void on_update_dtr(routing_t *self, const rt_sibl_update_dtr_t *event) {
    rt_forwarder_state_t *state = GET_STATE(self);
    if (!node_network_is_valid(&state->node_network, self->orientation)) return;

    uint32_t peer_dtr = event->dtr;
    if (peer_dtr == 0) {
        log_error(TAG, "[sibl_dtr_update] wrong dtr received");
    } else if ((state->dtr == 0) || (peer_dtr < state->dtr)) {
        state->dtr = peer_dtr;
        state->is_local_root = false;
        state->global_state = GLOBAL_STATE_WITH_NETWORK;
        state->gateway_request_timeout = 0;
        if (state->local_state == LOCAL_STATE_CONNECTED) {
            rt_peer_event_t peer_event = {
                .event_id = PEER_UPDATE_DTR,
                .payload.update_dtr = {
                    .dtr = state->dtr,
                },
            };

            wl_send_peer_message(self->deps.wl, &peer_event, sizeof(peer_event));
        }
    } else {
        log_error(TAG, "[sibl_dtr_update] Worse DTR received (%u > %u)", peer_dtr, state->dtr);
    }
}

static void on_provision(routing_t *self, const rt_sibl_provision_t *event) {
    rt_forwarder_state_t *state = GET_STATE(self);

    if (node_network_is_valid(&state->node_network, self->orientation)) {
        log_info(TAG, "[on_provision] Device already provisioned -- skipping new provision");
        return;
    }

    network_t device_network = get_node_subnet(&event->network, self->orientation);
    if (device_network.mask == 0 || event->dtr == 0)
        return;

    log_info(
        TAG, "[on_provision] Provisioned: %08X/%u by %u [dtr=%u]",  //
        event->network.addr, mask_size(event->network.mask), event->provider_id, event->dtr
    );

    state->dtr = event->dtr;
    state->device_network = device_network;
    state->node_network = event->network;
    state->global_state = GLOBAL_STATE_WITH_NETWORK;
    state->is_local_root = false;

    wl_enable_ap_mode(self->deps.wl, state->device_network.addr, state->device_network.mask);
}

static void on_send_new_gateway_request(routing_t *self, const rt_sibl_send_new_gateway_request_t *event) {
    rt_forwarder_state_t *state = GET_STATE(self);
    if (!node_network_is_valid(&state->node_network, self->orientation)) return;

    if (state->dtr == 1)
        return;  // I am root

    if (state->gateway_request_timeout != 0) {
        rt_gw_trace(self, RT_GW_DECISION, RT_GW_PENDING, 0, event->hag_networks, 16);
        return;
    }  // An outstanding request already has a retry deadline.

    if (state->local_state == LOCAL_STATE_NOT_CONNECTED) {
        rt_gw_trace(self, RT_GW_DECISION, RT_GW_NOT_CONNECTED, 0, event->hag_networks, 16);
        return;
    }  // We don't have a connection to a peer

    rt_peer_event_t peer_event = { .event_id = PEER_NEW_GATEWAY_REQUEST };
    memcpy(peer_event.payload.new_gateway_request.hag_networks, event->hag_networks, sizeof(event->hag_networks));

    network_t *hag_network = find_free_spot(
        peer_event.payload.new_gateway_request.hag_networks,
        sizeof(event->hag_networks) / sizeof(event->hag_networks[0])
    );

    if (!hag_network) {
        rt_gw_trace(self, RT_GW_DECISION, RT_GW_PATH_FULL, 0, event->hag_networks, 16);
        log_error(TAG, "[on_send_gw_req] Too many networks in message -- dropping");
        return;
    }

    if (rt_fwd_default_missing(self)) {
        state->global_state = GLOBAL_STATE_ON_GW_REQUEST;
        state->dtr = 0;
        state->is_local_root = false;
    }
    *hag_network = state->node_network;
    memcpy(state->gateway_request_path, peer_event.payload.new_gateway_request.hag_networks,
           sizeof(state->gateway_request_path));
    state->gateway_request_timeout = GATEWAY_REQUEST_RETRY_MS;

    bool sent=wl_send_peer_message(self->deps.wl, &peer_event, sizeof(peer_event));
    rt_gw_trace(self, RT_GW_REQUEST_TX, RT_GW_OK, sent,
        peer_event.payload.new_gateway_request.hag_networks, 16);
}

static bool confirmed_path_covers(const network_t requested[16], const network_t confirmed[16]) {
    if (!requested[0].addr) return false;
    for (size_t i = 0; i < 16 && requested[i].addr; ++i) {
        if (requested[i].addr != confirmed[i].addr || requested[i].mask != confirmed[i].mask)
            return false;
    }
    return true;
}

static void reply_pending_gateway(routing_t *self, uint32_t dtr, const network_t confirmed_path[16]) {
    rt_forwarder_state_t *state = GET_STATE(self);
    if (!state->gateway_relay_pending || state->local_state != LOCAL_STATE_CONNECTED ||
        !confirmed_path_covers(state->gateway_relay_path, confirmed_path))
        return;
    rt_peer_event_t response = {
        .event_id = PEER_NEW_GATEWAY_RESPONSE,
        .payload.new_gateway_response = {
            .dtr = dtr, .external_network = state->node_network,
        },
    };
    bool sent = wl_send_peer_message(self->deps.wl, &response, sizeof(response));
    rt_gw_trace(self, RT_GW_RESPONSE_TX, RT_GW_OK, sent, NULL, 0);
    if (sent) state->gateway_relay_pending = false;
}

static void on_gateway_confirmation(routing_t *self, const rt_sibl_new_gateway_winner_t *event) {
    rt_forwarder_state_t *state = GET_STATE(self);
    if (!node_network_is_valid(&state->node_network, self->orientation) ||
        !event->dtr || event->dtr == UINT32_MAX || rt_fwd_default_missing(self) ||
        event->network.addr != state->node_network.addr ||
        event->network.mask != state->node_network.mask)
        return;
    /* Confirmation serves pending peers, not a new election. Synchronize the
     * actual distance of the already selected parent, which may itself have
     * switched to a longer path; neither the output nor its owner changes. */
    if (state->dtr == 0) state->is_local_root = false;
    state->dtr = event->dtr;
    state->global_state = GLOBAL_STATE_WITH_NETWORK;
    if (confirmed_path_covers(state->gateway_request_path, event->confirmed_path))
        state->gateway_request_timeout = 0;
    reply_pending_gateway(self, event->dtr, event->confirmed_path);
}

static void on_new_gateway_winner(routing_t *self, const rt_sibl_new_gateway_winner_t *event) {
    rt_forwarder_state_t *state = GET_STATE(self);
    if (!node_network_is_valid(&state->node_network, self->orientation)) return;

    if (state->dtr == 1) {  // I'm root
        /* Root installed every network in this radio's received NGR before
         * asking its center for a response. It can answer that exact path. */
        reply_pending_gateway(self, state->dtr, state->gateway_relay_path);
        return;
    }

    state->global_state = GLOBAL_STATE_WITH_NETWORK;
    state->is_local_root = false;
    // The winning interface already counted the wireless hop for this node.
    state->dtr = event->dtr;
    if (confirmed_path_covers(state->gateway_request_path, event->confirmed_path))
        state->gateway_request_timeout = 0;

    reply_pending_gateway(self, state->dtr, event->confirmed_path);
}

void rt_fwd_sibl_set_required_callbacks(rt_role_impl_t *impl) {
    impl->on_sibl_update_dtr = on_update_dtr;
    impl->on_sibl_provision = on_provision;
    impl->on_sibl_send_new_gateway_request = on_send_new_gateway_request;
    impl->on_sibl_new_gateway_winner = on_new_gateway_winner;
    impl->on_sibl_gateway_confirmation = on_gateway_confirmation;
}
