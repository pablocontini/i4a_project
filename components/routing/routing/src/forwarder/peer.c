#include "forwarder.h"

#include <string.h>

#include "node.h"
#include "../utils.h"
#include "os/os.h"

#define UNUSED(x) ((void)x)

static bool valid_network(const network_t *network, uint32_t max_prefix) {
    uint32_t host_mask = ~network->mask;
    return network->addr != 0 && network->mask != 0 &&
           (host_mask & (host_mask + 1)) == 0 &&
           (network->addr & network->mask) == network->addr &&
           mask_size(network->mask) <= max_prefix;
}

static void on_peer_connected(routing_t *self, const network_t *connection) {
    UNUSED(connection);

    rt_forwarder_state_t *state = GET_STATE(self);

    state->local_state = LOCAL_STATE_CONNECTED;
    rt_peer_event_t peer_event = {
        .event_id = PEER_HANDSHAKE,
        .payload.handshake = {
            .external_network = state->node_network,
            .provided_network = state->device_network,
            .dtr = state->dtr,
        },
    };

    bool sent=wl_send_peer_message(self->deps.wl,&peer_event,sizeof(peer_event));
    os_diag("gateway","event=handshake_send orientation=%u dtr=%lu sent=%u",self->orientation,(unsigned long)state->dtr,sent);
    if (state->node_network.mask && state->dtr == 0) {
        const network_t empty_path[16] = {0};
        if (rt_fwd_prepare_gateway_request(self, empty_path)) {
            state->global_state = GLOBAL_STATE_ON_GW_REQUEST;
            rt_fwd_send_gateway_request(self);
        }
    }
}

static void on_handshake(routing_t *self, const rt_peer_handshake_t *event) {
    rt_forwarder_state_t *state = GET_STATE(self);

    if (state->node_network.mask == 0) {
        if (!event->dtr || event->dtr >= UINT32_MAX - 1 ||
            !valid_network(&event->external_network, 32) ||
            !valid_network(&event->provided_network, 26) ||
            event->provided_network.mask <= event->external_network.mask ||
            (event->provided_network.addr & event->external_network.mask) != event->external_network.addr)
            return;
        // Provision node
        // Crossing a wireless link adds one node; SPI siblings share this DTR.
        state->dtr = event->dtr + 1;
        state->device_network = get_node_subnet(&event->provided_network, self->orientation);
        state->node_network = event->provided_network;
        state->global_state = GLOBAL_STATE_WITH_NETWORK;
        state->is_local_root = true;

        // Update my own subnet/mask
        node_set_network_settings(state->device_network.addr, state->device_network.mask);

        // Update routing table
        add_global_route(self, &NETWORK(0, 0), self->orientation);

        rt_sibl_event_t provision = {
            .event_id = SIBL_PROVISION,
            .payload.provision = {
                .network = state->node_network,
                .provider_id = self->orientation,
                .dtr = state->dtr,
            },
        };

        rs_broadcast(self->deps.rs, RS_ROUTING, &provision, sizeof(provision));
    } else {
        // We already have a network, add a redundant route to the table
        if (valid_network(&event->external_network, 32))
            add_global_route(self, &event->external_network, self->orientation);
    }
}

static void on_update_dtr(routing_t *self, const rt_peer_update_dtr_t *event) {
    rt_forwarder_state_t *state = GET_STATE(self);

    uint32_t peer_dtr = event->dtr;
    uint32_t my_dtr = state->dtr;

    if (state->local_state != LOCAL_STATE_CONNECTED || peer_dtr == 0 || peer_dtr == UINT32_MAX) {
        // Peer is not connected to the network
        return;
    }

    if ((my_dtr == 0) || (peer_dtr + 1 < my_dtr)) {
        // I'm not connected to the network or I can improve my DTR
        state->dtr = peer_dtr + 1;
        state->is_local_root = true;
        state->global_state = GLOBAL_STATE_WITH_NETWORK;
        state->gateway_retry_timeout = 0;
        add_global_route(self, &NETWORK(0, 0), self->orientation);

        rt_sibl_event_t event = {
            .event_id = SIBL_UPDATE_DTR,
            .payload.update_dtr.dtr = peer_dtr + 1,
        };
        rs_broadcast(self->deps.rs, RS_ROUTING, &event, sizeof(event));
    }
}

static void on_new_gateway_request(routing_t *self, const rt_peer_new_gateway_request_t *event) {
    rt_forwarder_state_t *state = GET_STATE(self);

    if (state->local_state != LOCAL_STATE_CONNECTED || !state->node_network.mask)
        return;
    os_diag("gateway","event=request_received orientation=%u dtr=%lu",self->orientation,(unsigned long)state->dtr);
    for (size_t i = 0; i < 16 && event->hag_networks[i].addr; ++i) {
        if (event->hag_networks[i].addr == state->node_network.addr &&
            event->hag_networks[i].mask == state->node_network.mask)
            { os_diag("gateway","event=request_loop_rejected orientation=%u",self->orientation); return; }
    }

    for (uint32_t i = 0; i < sizeof(event->hag_networks) / sizeof(event->hag_networks[0]); i++) {
        // The zero-filled tail terminates the path; it is not a default route.
        if (event->hag_networks[i].addr == 0)
            break;

        add_global_route(self, &event->hag_networks[i], self->orientation);
    }

    rt_sibl_event_t sibl_event = {
        .event_id = SIBL_SEND_NEW_GATEWAY_REQUEST,
    };

    _Static_assert(  // Ensure we won't generate a buffer overflow in the memcpy
        sizeof(sibl_event.payload.send_new_gateway_request.hag_networks) >= sizeof(event->hag_networks),
        "The sibling event structure supports fewer hag_networks than the peer event counterpart"
    );
    memcpy(&sibl_event.payload.send_new_gateway_request.hag_networks, event->hag_networks, sizeof(event->hag_networks));

    if (state->dtr == 1) {
        // I am root
        rs_broadcast(self->deps.rs, RS_ROUTING, &sibl_event, sizeof(sibl_event));
        return;
    }

    if (state->global_state == GLOBAL_STATE_ON_GW_REQUEST)
        return;

    if (!rt_fwd_prepare_gateway_request(self, event->hag_networks))
        return;
    state->global_state = GLOBAL_STATE_ON_GW_REQUEST;
    state->is_local_root = false;
    state->dtr = 0;
    add_global_route(self, &NETWORK(0, 0), ROUTING_OUTPUT_NONE);
    rs_broadcast(self->deps.rs, RS_ROUTING, &sibl_event, sizeof(sibl_event));
}

static void on_new_gateway_response(routing_t *self, const rt_peer_new_gateway_response_t *event) {
    rt_forwarder_state_t *state = GET_STATE(self);

    uint32_t peer_dtr = event->dtr;

    if (state->local_state != LOCAL_STATE_CONNECTED || peer_dtr == 0 || peer_dtr == UINT32_MAX) {
        return;
    }
    uint32_t candidate_dtr = peer_dtr + 1;
    os_diag("gateway","event=response_received orientation=%u candidate_dtr=%lu current_dtr=%lu",self->orientation,(unsigned long)candidate_dtr,(unsigned long)state->dtr);

    if ((state->dtr != 0) && (state->dtr <= candidate_dtr)) {
        // Keep the current gateway when the candidate is equal or worse.
        return;
    }

    state->global_state = GLOBAL_STATE_WITH_NETWORK;
    state->is_local_root = true;
    state->dtr = candidate_dtr;
    state->gateway_retry_timeout = 0;

    add_global_route(self, &NETWORK(0, 0), self->orientation);
    rt_sibl_event_t sibl_event = {
        .event_id = SIBL_NEW_GATEWAY_WINNER,
        .payload.new_gateway_winner.dtr = state->dtr,
        .payload.new_gateway_winner.network = event->external_network,
    };
    rs_broadcast(self->deps.rs, RS_ROUTING, &sibl_event, sizeof(sibl_event));
}

static void on_peer_lost(routing_t *self, const network_t *connection) {
    UNUSED(connection);

    rt_forwarder_state_t *state = GET_STATE(self);

    os_diag("gateway","event=loss_processed orientation=%u was_gateway=%u dtr=%lu",self->orientation,state->is_local_root,(unsigned long)state->dtr);
    state->local_state = LOCAL_STATE_NOT_CONNECTED;
    remove_routes_by_output(self, self->orientation);

    // Losing a nondefault link also withdraws this node's return route at
    // its peer. Reannounce the path through surviving siblings, even when
    // a previous recovery already selected another default gateway.
    // Root radios have DTR=1 and must retain the central's internet gateway.
    if (state->is_local_root || (state->node_network.mask && state->dtr > 1)) {
        os_diag("gateway", "event=return_path_recovery orientation=%u", self->orientation);
        log_info(TAG, "[peer_lost] Rebuilding gateway and return path");
        state->is_local_root = false;
        state->dtr = 0;
        state->global_state = GLOBAL_STATE_ON_GW_REQUEST;
        const network_t empty_path[16] = {0};
        rt_fwd_prepare_gateway_request(self, empty_path);
        rt_sibl_event_t sibl_event = {
            .event_id = SIBL_SEND_NEW_GATEWAY_REQUEST,
        };
        rs_broadcast(self->deps.rs, RS_ROUTING, &sibl_event, sizeof(sibl_event));
    }
}

void rt_fwd_peer_set_required_callbacks(rt_role_impl_t *impl) {
    impl->on_peer_connected = on_peer_connected;
    impl->on_peer_handshake = on_handshake;
    impl->on_peer_update_dtr = on_update_dtr;
    impl->on_peer_new_gateway_request = on_new_gateway_request;
    impl->on_peer_new_gateway_response = on_new_gateway_response;
    impl->on_peer_lost = on_peer_lost;

}
