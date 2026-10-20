#include "forwarder.h"

#include <string.h>

#include "node.h"
#include "../utils.h"
#include "os/os.h"

#define UNUSED(x) ((void)x)

static void on_update_dtr(routing_t *self, const rt_peer_update_dtr_t *event);

void rt_fwd_publish_provision(routing_t *self) {
    rt_forwarder_state_t *state = GET_STATE(self);
    rt_sibl_event_t provision = {
        .event_id = SIBL_PROVISION,
        .payload.provision = {
            .network = state->node_network,
            .provider_id = self->orientation,
            .dtr = state->dtr,
        },
    };
    /* Failed return ACKs may follow a partial delivery. Recipients already
     * provisioned ignore the replay, while missing siblings can still join. */
    state->provision_retry_timeout =
        rs_broadcast(self->deps.rs, RS_ROUTING, &provision, sizeof(provision))
            ? 0 : PROVISION_RETRY_MS;
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

    wl_send_peer_message(self->deps.wl, &peer_event, sizeof(peer_event));
}

static void on_handshake(routing_t *self, const rt_peer_handshake_t *event) {
    rt_forwarder_state_t *state = GET_STATE(self);

    if (state->global_state == GLOBAL_STATE_WITHOUT_NETWORK) {
        if (event->dtr == 0 || event->dtr >= UINT16_MAX)
            return;
        network_t device_network = get_node_subnet(&event->provided_network, self->orientation);
        if (device_network.mask == 0)
            return;
        // Provision node
        // Crossing a wireless link adds one node; SPI siblings share this DTR.
        state->dtr = event->dtr + 1;
        state->device_network = device_network;
        state->node_network = event->provided_network;
        state->global_state = GLOBAL_STATE_WITH_NETWORK;
        state->is_local_root = true;

        // Update my own subnet/mask
        node_set_network_settings(state->device_network.addr, state->device_network.mask);

        // Update routing table
        add_global_route(self, &NETWORK(0, 0), self->orientation);

        rt_fwd_publish_provision(self);
    } else {
        /* Both ends send their handshake on admission. The unprovisioned end
         * advertises 0/0; that is not a route and must never replace our WAN
         * gateway while we are providing its first network. */
        if (event->external_network.addr == 0 || event->external_network.mask == 0)
            return;
        // We already have a network, add a redundant route to the table
        add_global_route(self, &event->external_network, self->orientation);
        /* A reconnect does not reprovision this node. It still needs to
         * restore its gateway when the handshake advertises a usable path. */
        rt_peer_update_dtr_t update = { .dtr = event->dtr };
        on_update_dtr(self, &update);
    }
}

static void on_update_dtr(routing_t *self, const rt_peer_update_dtr_t *event) {
    rt_forwarder_state_t *state = GET_STATE(self);

    uint32_t peer_dtr = event->dtr;
    uint32_t my_dtr = state->dtr;

    if (peer_dtr == 0 || peer_dtr == UINT32_MAX) {
        // Peer is not connected to the network
        return;
    }

    if ((my_dtr == 0) || (peer_dtr + 1 < my_dtr)) {
        // I'm not connected to the network or I can improve my DTR
        state->dtr = peer_dtr + 1;
        state->is_local_root = true;
        state->global_state = GLOBAL_STATE_WITH_NETWORK;
        state->gateway_request_timeout = 0;
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

    /* A repeated path must not install a route to this node through a peer. */
    for (size_t i = 0; i < sizeof(event->hag_networks) / sizeof(event->hag_networks[0]); ++i) {
        if (event->hag_networks[i].addr == 0)
            break;
        if (event->hag_networks[i].addr == state->node_network.addr &&
            event->hag_networks[i].mask == state->node_network.mask)
            return;
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

    state->global_state = GLOBAL_STATE_ON_GW_REQUEST;
    state->dtr = 0;
    rs_broadcast(self->deps.rs, RS_ROUTING, &sibl_event, sizeof(sibl_event));
}

static void on_new_gateway_response(routing_t *self, const rt_peer_new_gateway_response_t *event) {
    rt_forwarder_state_t *state = GET_STATE(self);

    uint32_t peer_dtr = event->dtr;

    if (peer_dtr == 0 || peer_dtr == UINT32_MAX) {
        return;
    }
    uint32_t candidate_dtr = peer_dtr + 1;

    if ((state->dtr != 0) && (state->dtr <= candidate_dtr)) {
        // Keep the current gateway when the candidate is equal or worse.
        return;
    }

    state->global_state = GLOBAL_STATE_WITH_NETWORK;
    state->is_local_root = true;
    state->dtr = candidate_dtr;
    state->gateway_request_timeout = 0;

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

    state->local_state = LOCAL_STATE_NOT_CONNECTED;
    remove_routes_by_output(self, self->orientation);

    /* A lost redundant link can remove the upstream node's return route even
     * when this radio was no longer the default gateway. Reannounce the path
     * through the surviving radios in that case as well. */
    if (state->is_local_root || state->dtr > 1) {
        log_info(TAG, "[peer_lost] Connection to ROOT node has been lost");
        state->is_local_root = false;
        state->dtr = 0;
        state->global_state = GLOBAL_STATE_ON_GW_REQUEST;
        state->gateway_request_timeout = 0;
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
