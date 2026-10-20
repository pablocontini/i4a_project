#include "routing/routing.h"

#include "os/os.h"

#include "impl_priv.h"
#include "routing/impl.h"
#include "routing/routing_table.h"
#include "utils.h"

#define TAG "root"
#define GET_STATE(self) (&((self)->role.state.root))

#define GATEWAY_REQUEST_TIMEOUT 10000  // 10 seconds
#define ROOT_CONTROL_RETRY_MS 5000

static void publish_provision(routing_t *self) {
    rt_root_state_t *state = GET_STATE(self);
    rt_sibl_event_t provision = {
        .event_id = SIBL_PROVISION,
        .payload.provision = {
            .network = state->network,
            .provider_id = ORIENTATION_CENTER,
            .dtr = 1,
        },
    };

    /* Packet loss is a transport outcome, not a fatal firmware invariant. */
    state->provision_retry_timeout =
        rs_broadcast(self->deps.rs, RS_ROUTING, &provision, sizeof(provision))
            ? 0 : ROOT_CONTROL_RETRY_MS;
}

static void on_start(routing_t *self) {
    log_info(TAG, "on_start");
    // Center device remains the default gateway during provisioning retries.
    add_global_route(self, &NETWORK(0, 0), self->orientation);
    publish_provision(self);
}

static void on_new_gateway_request(routing_t *self, const rt_sibl_send_new_gateway_request_t *unused) {
    (void)unused;
    rt_root_state_t *state = GET_STATE(self);

    if (state->gateway_requested)
        return;

    state->gateway_requested = true;
    state->gateway_requested_timeout = GATEWAY_REQUEST_TIMEOUT;
}

static void on_tick(routing_t *self, uint32_t dt_ms) {
    rt_root_state_t *state = GET_STATE(self);

    if (state->provision_retry_timeout) {
        if (dt_ms < state->provision_retry_timeout)
            state->provision_retry_timeout -= dt_ms;
        else
            publish_provision(self);
    }

    if (!state->gateway_requested)
        return;

    if (dt_ms >= state->gateway_requested_timeout) {
        state->gateway_requested = false;
        state->gateway_requested_timeout = 0;

        log_info(TAG, "[on_tick] Wait finished -- responding NGRs");
        rt_sibl_event_t event = {
            .event_id = SIBL_NEW_GATEWAY_WINNER,
            .payload.new_gateway_winner = {
                .network = state->network,
                .dtr = 1,
            },
        };
        if (!rs_broadcast(self->deps.rs, RS_ROUTING, &event, sizeof(event))) {
            state->gateway_requested = true;
            state->gateway_requested_timeout = ROOT_CONTROL_RETRY_MS;
        }
    } else {
        // Continue waiting
        state->gateway_requested_timeout -= dt_ms;
    }
}

bool create_root_core(routing_t *self, uint32_t root_network, uint32_t root_mask) {
    network_t network = NETWORK(root_network, root_mask);
    if (!node_network_is_valid(&network, ORIENTATION_CENTER))
        return false;
    rt_root_state_t *state = GET_STATE(self);

    state->network = network;
    state->gateway_requested = false;
    state->gateway_requested_timeout = 0;
    state->provision_retry_timeout = 0;

    self->role.impl = (rt_role_impl_t){
        .on_start = on_start,
        .on_tick = on_tick,
        .on_sibl_send_new_gateway_request = on_new_gateway_request,
    };

    return true;
}
